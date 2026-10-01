// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "procfs.hpp"
#include <arpa/inet.h>
#include <dirent.h>
#include <format>
#include <fstream>
#include <sstream>
#include <iostream>

namespace leakspot {

std::string ProcFs::parse_tcp_state(int state_code) {
    switch (state_code) {
        case 1:  return "ESTABLISHED";
        case 2:  return "SYN_SENT";
        case 3:  return "SYN_RECV";
        case 4:  return "FIN_WAIT1";
        case 5:  return "FIN_WAIT2";
        case 6:  return "TIME_WAIT";
        case 7:  return "CLOSE";
        case 8:  return "CLOSE_WAIT";
        case 9:  return "LAST_ACK";
        case 10: return "LISTEN";
        case 11: return "CLOSING";
        default: return "UNKNOWN";
    }
}

std::string ProcFs::format_ip_port(const std::string& hex_addr, int port) {
    if (hex_addr.length() == 8) {
        // IPv4 (little-endian hex)
        unsigned int raw_ip = 0;
        if (sscanf(hex_addr.c_str(), "%x", &raw_ip) == 1) {
            struct in_addr in;
            in.s_addr = raw_ip;
            char buf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &in, buf, sizeof(buf))) {
                return std::format("{}:{}", buf, port);
            }
        }
    }
    return std::format("{}:{}", hex_addr, port);
}

std::string ProcFs::read_cmdline(pid_t pid) {
    std::string path = std::format("/proc/{}/cmdline", pid);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    std::string cmdline;
    char ch;
    while (file.get(ch)) {
        if (ch == '\0') cmdline += ' ';
        else cmdline += ch;
    }
    if (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    return cmdline;
}

std::vector<ThreadStats> ProcFs::read_threads(pid_t pid) {
    std::vector<ThreadStats> threads;
    std::string task_dir = std::format("/proc/{}/task", pid);
    DIR* dir = opendir(task_dir.c_str());
    if (!dir) return threads;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        pid_t tid = static_cast<pid_t>(std::atoi(entry->d_name));
        if (tid <= 0) continue;

        ThreadStats ts;
        ts.tid = tid;

        // Read thread name from /proc/[pid]/task/[tid]/comm
        std::string comm_path = std::format("{}/{}/comm", task_dir, entry->d_name);
        std::ifstream cfile(comm_path);
        if (cfile.is_open()) {
            std::getline(cfile, ts.name);
        }

        // Read stat for thread
        std::string stat_path = std::format("{}/{}/stat", task_dir, entry->d_name);
        std::ifstream sfile(stat_path);
        if (sfile.is_open()) {
            std::string content;
            std::getline(sfile, content);
            auto close_paren = content.rfind(')');
            if (close_paren != std::string::npos && close_paren + 2 < content.length()) {
                std::istringstream iss(content.substr(close_paren + 2));
                char state;
                int ppid, pgrp, session, tty_nr, tpgid;
                unsigned int flags;
                uint64_t minflt, cminflt, majflt, cmajflt, utime, stime;
                long cutime, cstime, priority;
                if (iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
                        >> minflt >> cminflt >> majflt >> cmajflt
                        >> utime >> stime >> cutime >> cstime >> priority) {
                    ts.state = state;
                    ts.utime_ticks = utime;
                    ts.stime_ticks = stime;
                    ts.priority = priority;
                }
            }
        }
        threads.push_back(ts);
    }
    closedir(dir);
    return threads;
}

void parse_proc_net_file(const std::string& path, const std::string& proto, std::unordered_map<ino_t, SocketDetails>& sockets) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    // Skip header line
    std::getline(file, line);

    while (std::getline(file, line)) {
        char local_hex[64], rem_hex[64];
        int local_port = 0, rem_port = 0, state_code = 0;
        unsigned long inode = 0;

        // Format: sl: local_addr:port rem_addr:port st ... inode
        if (sscanf(line.c_str(), "%*d: %63[^:]:%X %63[^:]:%X %X %*x:%*x %*x:%*x %*x %*d %*d %lu",
                   local_hex, &local_port, rem_hex, &rem_port, &state_code, &inode) >= 6) {
            SocketDetails sd;
            sd.proto = proto;
            sd.local_addr = ProcFs::format_ip_port(local_hex, local_port);
            sd.local_port = local_port;
            sd.remote_addr = ProcFs::format_ip_port(rem_hex, rem_port);
            sd.remote_port = rem_port;
            sd.state = ProcFs::parse_tcp_state(state_code);
            sd.inode = static_cast<ino_t>(inode);
            sockets[sd.inode] = sd;
        }
    }
}

std::unordered_map<ino_t, SocketDetails> ProcFs::parse_system_sockets() {
    std::unordered_map<ino_t, SocketDetails> sockets;
    parse_proc_net_file("/proc/net/tcp", "tcp", sockets);
    parse_proc_net_file("/proc/net/tcp6", "tcp6", sockets);
    parse_proc_net_file("/proc/net/udp", "udp", sockets);
    parse_proc_net_file("/proc/net/udp6", "udp6", sockets);
    return sockets;
}

uint64_t ProcFs::read_system_total_cpu_ticks() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0;

    std::string line;
    if (std::getline(file, line)) {
        if (line.rfind("cpu ", 0) == 0) {
            std::istringstream iss(line.substr(4));
            uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
            if (iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
                return user + nice + system + idle + iowait + irq + softirq + steal;
            }
        }
    }
    return 0;
}

} // namespace leakspot

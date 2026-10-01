// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "sampler.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <format>
#include <iostream>

namespace leakspot {

bool Sampler::is_alive(pid_t pid) {
    if (pid <= 0) return false;
    if (kill(pid, 0) == 0) return true;
    return errno != ESRCH;
}

bool Sampler::parse_smaps_rollup(pid_t pid, MemoryStats& mem) {
    std::string path = std::format("/proc/{}/smaps_rollup", pid);
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    bool found_any = false;
    while (std::getline(file, line)) {
        char key[64];
        uint64_t val = 0;
        char unit[16];
        if (sscanf(line.c_str(), "%63[^:]: %lu %15s", key, &val, unit) >= 2) {
            uint64_t bytes = val * 1024; // smaps_rollup is in kB
            found_any = true;
            if (strcmp(key, "Rss") == 0) mem.rss_bytes = bytes;
            else if (strcmp(key, "Pss") == 0) mem.pss_bytes = bytes;
            else if (strcmp(key, "Anonymous") == 0) mem.anon_bytes = bytes;
            else if (strcmp(key, "Shared_Clean") == 0) mem.shared_clean = bytes;
            else if (strcmp(key, "Shared_Dirty") == 0) mem.shared_dirty = bytes;
            else if (strcmp(key, "Private_Clean") == 0) mem.private_clean = bytes;
            else if (strcmp(key, "Private_Dirty") == 0) mem.private_dirty = bytes;
            else if (strcmp(key, "Referenced") == 0) mem.referenced_bytes = bytes;
            else if (strcmp(key, "Locked") == 0) mem.locked_bytes = bytes;
            else if (strcmp(key, "Swap") == 0) mem.swap_bytes = bytes;
            else if (strcmp(key, "SwapPss") == 0) mem.swap_pss_bytes = bytes;
        }
    }
    return found_any;
}

bool Sampler::parse_status(pid_t pid, ProcessStats& stats) {
    std::string path = std::format("/proc/{}/status", pid);
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        char key[64];
        uint64_t val = 0;
        if (sscanf(line.c_str(), "%63[^:]: %lu", key, &val) >= 2) {
            uint64_t bytes = val * 1024;
            if (strcmp(key, "VmPeak") == 0) stats.mem.vm_peak_bytes = bytes;
            else if (strcmp(key, "VmSize") == 0) stats.mem.vm_size_bytes = bytes;
            else if (strcmp(key, "VmHWM") == 0) stats.mem.vm_hwm_bytes = bytes;
            else if (strcmp(key, "VmRSS") == 0) {
                if (stats.mem.rss_bytes == 0) stats.mem.rss_bytes = bytes;
            } else if (strcmp(key, "RssAnon") == 0) {
                if (stats.mem.anon_bytes == 0) stats.mem.anon_bytes = bytes;
            } else if (strcmp(key, "VmData") == 0) stats.mem.vm_data_bytes = bytes;
            else if (strcmp(key, "VmStk") == 0) stats.mem.vm_stk_bytes = bytes;
            else if (strcmp(key, "VmExe") == 0) stats.mem.vm_exe_bytes = bytes;
            else if (strcmp(key, "VmLib") == 0) stats.mem.vm_lib_bytes = bytes;
            else if (strcmp(key, "VmPTE") == 0) stats.mem.vm_pte_bytes = bytes;
            else if (strcmp(key, "VmSwap") == 0) {
                if (stats.mem.swap_bytes == 0) stats.mem.swap_bytes = bytes;
            } else if (strcmp(key, "Threads") == 0) {
                stats.num_threads = static_cast<int>(val);
            } else if (strcmp(key, "voluntary_ctxt_switches") == 0) {
                stats.voluntary_ctxt_switches = val;
            } else if (strcmp(key, "nonvoluntary_ctxt_switches") == 0) {
                stats.nonvoluntary_ctxt_switches = val;
            }
        }
    }
    return true;
}

bool Sampler::parse_stat(pid_t pid, ProcessStats& stats) {
    std::string path = std::format("/proc/{}/stat", pid);
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content;
    std::getline(file, content);
    if (content.empty()) return false;

    auto open_paren = content.find('(');
    auto close_paren = content.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos || close_paren <= open_paren) {
        return false;
    }

    stats.comm = content.substr(open_paren + 1, close_paren - open_paren - 1);

    std::string rest = content.substr(close_paren + 2);
    std::istringstream iss(rest);

    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    uint64_t minflt, cminflt, majflt, cmajflt, utime, stime;
    long cutime, cstime, priority, nice, num_threads;

    if (iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
            >> minflt >> cminflt >> majflt >> cmajflt
            >> utime >> stime >> cutime >> cstime
            >> priority >> nice >> num_threads) {
        stats.state = state;
        stats.ppid = ppid;
        stats.num_threads = static_cast<int>(num_threads);
        stats.utime_ticks = utime;
        stats.stime_ticks = stime;
        stats.mem.minor_faults = minflt;
        stats.mem.major_faults = majflt;
        return true;
    }

    return false;
}

bool Sampler::parse_fds(pid_t pid, FdStats& fds, const std::unordered_map<ino_t, SocketDetails>& sockets_map) {
    std::string fd_dir = std::format("/proc/{}/fd", pid);
    DIR* dir = opendir(fd_dir.c_str());
    if (!dir) return false;

    struct dirent* entry = nullptr;
    char target[1024];

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        ++fds.total_fds;
        std::string link_path = fd_dir + "/" + entry->d_name;
        ssize_t len = readlink(link_path.c_str(), target, sizeof(target) - 1);
        if (len > 0) {
            target[len] = '\0';
            if (strncmp(target, "socket:[", 8) == 0) {
                ++fds.sockets;
                unsigned long inode = 0;
                if (sscanf(target + 8, "%lu", &inode) == 1) {
                    auto it = sockets_map.find(static_cast<ino_t>(inode));
                    if (it != sockets_map.end()) {
                        fds.active_sockets.push_back(it->second);
                        if (it->second.state == "ESTABLISHED") ++fds.sockets_established;
                        else if (it->second.state == "LISTEN") ++fds.sockets_listen;
                        else if (it->second.state == "CLOSE_WAIT") ++fds.sockets_close_wait;
                        else if (it->second.state == "TIME_WAIT") ++fds.sockets_time_wait;
                        else ++fds.sockets_other;
                    } else {
                        ++fds.sockets_other;
                    }
                } else {
                    ++fds.sockets_other;
                }
            } else if (strncmp(target, "pipe:[", 6) == 0) {
                ++fds.pipes;
            } else if (strncmp(target, "anon_inode:[", 12) == 0) {
                ++fds.anon_inodes;
            } else if (strncmp(target, "/dev/", 5) == 0) {
                ++fds.devices;
            } else if (target[0] == '/') {
                ++fds.files;
            } else {
                ++fds.other;
            }
        } else {
            ++fds.other;
        }
    }

    closedir(dir);
    return true;
}

std::optional<ProcessStats> Sampler::sample_process(pid_t pid, bool resolve_sockets) {
    if (!is_alive(pid)) {
        return std::nullopt;
    }

    ProcessStats stats;
    stats.pid = pid;

    if (!parse_stat(pid, stats)) {
        return std::nullopt;
    }

    parse_smaps_rollup(pid, stats.mem);
    parse_status(pid, stats);

    std::unordered_map<ino_t, SocketDetails> system_sockets;
    if (resolve_sockets) {
        system_sockets = ProcFs::parse_system_sockets();
    }

    parse_fds(pid, stats.fds, system_sockets);
    stats.threads = ProcFs::read_threads(pid);
    stats.cmdline = ProcFs::read_cmdline(pid);

    return stats;
}

std::vector<pid_t> Sampler::get_child_pids(pid_t parent_pid) {
    std::vector<pid_t> children;
    std::string task_dir = std::format("/proc/{}/task", parent_pid);
    DIR* dir = opendir(task_dir.c_str());
    if (!dir) return children;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string children_path = std::format("{}/{}/children", task_dir, entry->d_name);
        std::ifstream file(children_path);
        if (file.is_open()) {
            pid_t child_pid = 0;
            while (file >> child_pid) {
                children.push_back(child_pid);
            }
        }
    }
    closedir(dir);
    return children;
}

ProcessStats Sampler::aggregate_stats(const ProcessStats& parent, const std::vector<ProcessStats>& children) {
    ProcessStats agg = parent;
    for (const auto& ch : children) {
        agg.num_threads += ch.num_threads;
        agg.utime_ticks += ch.utime_ticks;
        agg.stime_ticks += ch.stime_ticks;
        agg.voluntary_ctxt_switches += ch.voluntary_ctxt_switches;
        agg.nonvoluntary_ctxt_switches += ch.nonvoluntary_ctxt_switches;

        agg.mem.rss_bytes += ch.mem.rss_bytes;
        agg.mem.pss_bytes += ch.mem.pss_bytes;
        agg.mem.anon_bytes += ch.mem.anon_bytes;
        agg.mem.swap_bytes += ch.mem.swap_bytes;
        agg.mem.swap_pss_bytes += ch.mem.swap_pss_bytes;
        agg.mem.vm_size_bytes += ch.mem.vm_size_bytes;
        agg.mem.vm_peak_bytes = std::max(agg.mem.vm_peak_bytes, ch.mem.vm_peak_bytes);
        agg.mem.vm_hwm_bytes = std::max(agg.mem.vm_hwm_bytes, ch.mem.vm_hwm_bytes);
        agg.mem.vm_data_bytes += ch.mem.vm_data_bytes;
        agg.mem.vm_stk_bytes += ch.mem.vm_stk_bytes;
        agg.mem.vm_exe_bytes += ch.mem.vm_exe_bytes;
        agg.mem.vm_lib_bytes += ch.mem.vm_lib_bytes;
        agg.mem.vm_pte_bytes += ch.mem.vm_pte_bytes;
        agg.mem.shared_clean += ch.mem.shared_clean;
        agg.mem.shared_dirty += ch.mem.shared_dirty;
        agg.mem.private_clean += ch.mem.private_clean;
        agg.mem.private_dirty += ch.mem.private_dirty;
        agg.mem.referenced_bytes += ch.mem.referenced_bytes;
        agg.mem.locked_bytes += ch.mem.locked_bytes;
        agg.mem.minor_faults += ch.mem.minor_faults;
        agg.mem.major_faults += ch.mem.major_faults;

        agg.fds.total_fds += ch.fds.total_fds;
        agg.fds.sockets += ch.fds.sockets;
        agg.fds.sockets_established += ch.fds.sockets_established;
        agg.fds.sockets_listen += ch.fds.sockets_listen;
        agg.fds.sockets_close_wait += ch.fds.sockets_close_wait;
        agg.fds.sockets_time_wait += ch.fds.sockets_time_wait;
        agg.fds.sockets_other += ch.fds.sockets_other;
        agg.fds.pipes += ch.fds.pipes;
        agg.fds.files += ch.fds.files;
        agg.fds.anon_inodes += ch.fds.anon_inodes;
        agg.fds.devices += ch.fds.devices;
        agg.fds.other += ch.fds.other;

        for (const auto& sock : ch.fds.active_sockets) {
            agg.fds.active_sockets.push_back(sock);
        }
        for (const auto& th : ch.threads) {
            agg.threads.push_back(th);
        }
    }
    return agg;
}

} // namespace leakspot

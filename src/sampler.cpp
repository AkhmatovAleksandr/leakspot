#include "sampler.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <format>
#include <iostream>

namespace leakspot {

bool Sampler::is_alive(pid_t pid) {
    if (pid <= 0) return false;
    // kill(pid, 0) checks if process exists and we have permission
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
            else if (strcmp(key, "Swap") == 0) mem.swap_bytes = bytes;
        }
    }
    return found_any;
}

bool Sampler::parse_status_fallback(pid_t pid, MemoryStats& mem) {
    std::string path = std::format("/proc/{}/status", pid);
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        char key[64];
        uint64_t val = 0;
        if (sscanf(line.c_str(), "%63[^:]: %lu", key, &val) >= 2) {
            uint64_t bytes = val * 1024;
            if (strcmp(key, "VmRSS") == 0) {
                if (mem.rss_bytes == 0) mem.rss_bytes = bytes;
            } else if (strcmp(key, "RssAnon") == 0) {
                if (mem.anon_bytes == 0) mem.anon_bytes = bytes;
            } else if (strcmp(key, "VmSwap") == 0) {
                if (mem.swap_bytes == 0) mem.swap_bytes = bytes;
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

    // Find comm inside parentheses
    auto open_paren = content.find('(');
    auto close_paren = content.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos || close_paren <= open_paren) {
        return false;
    }

    stats.comm = content.substr(open_paren + 1, close_paren - open_paren - 1);

    // Parse values after close_paren
    std::string rest = content.substr(close_paren + 2); // skip ") "
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
        stats.num_threads = static_cast<int>(num_threads);
        stats.utime_ticks = utime;
        stats.stime_ticks = stime;
        stats.mem.minor_faults = minflt;
        stats.mem.major_faults = majflt;
        return true;
    }

    return false;
}

bool Sampler::parse_fds(pid_t pid, FdStats& fds) {
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
            } else if (strncmp(target, "pipe:[", 6) == 0) {
                ++fds.pipes;
            } else if (strncmp(target, "anon_inode:[", 12) == 0) {
                ++fds.anon_inodes;
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

std::optional<ProcessStats> Sampler::sample_process(pid_t pid) {
    if (!is_alive(pid)) {
        return std::nullopt;
    }

    ProcessStats stats;
    stats.pid = pid;

    if (!parse_stat(pid, stats)) {
        return std::nullopt;
    }

    if (!parse_smaps_rollup(pid, stats.mem)) {
        // Fall back to /proc/[pid]/status
        parse_status_fallback(pid, stats.mem);
    }

    parse_fds(pid, stats.fds);

    return stats;
}

std::vector<pid_t> Sampler::get_child_pids(pid_t parent_pid) {
    std::vector<pid_t> children;

    // Scan /proc/[pid]/task/*/children
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

        agg.mem.rss_bytes += ch.mem.rss_bytes;
        agg.mem.pss_bytes += ch.mem.pss_bytes;
        agg.mem.anon_bytes += ch.mem.anon_bytes;
        agg.mem.swap_bytes += ch.mem.swap_bytes;
        agg.mem.shared_clean += ch.mem.shared_clean;
        agg.mem.shared_dirty += ch.mem.shared_dirty;
        agg.mem.private_clean += ch.mem.private_clean;
        agg.mem.private_dirty += ch.mem.private_dirty;
        agg.mem.minor_faults += ch.mem.minor_faults;
        agg.mem.major_faults += ch.mem.major_faults;

        agg.fds.total_fds += ch.fds.total_fds;
        agg.fds.sockets += ch.fds.sockets;
        agg.fds.pipes += ch.fds.pipes;
        agg.fds.files += ch.fds.files;
        agg.fds.anon_inodes += ch.fds.anon_inodes;
        agg.fds.other += ch.fds.other;
    }
    return agg;
}

} // namespace leakspot

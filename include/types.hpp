#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

namespace leakspot {

struct MemoryStats {
    uint64_t rss_bytes{0};       // Resident Set Size (physical memory)
    uint64_t pss_bytes{0};       // Proportional Set Size (accounting for shared pages)
    uint64_t anon_bytes{0};      // Anonymous/heap allocations (primary leak indicator)
    uint64_t swap_bytes{0};      // Swapped out pages
    uint64_t shared_clean{0};
    uint64_t shared_dirty{0};
    uint64_t private_clean{0};
    uint64_t private_dirty{0};
    uint64_t minor_faults{0};    // Page reclaims without disk I/O
    uint64_t major_faults{0};    // Page faults requiring disk I/O
};

struct FdStats {
    int total_fds{0};            // Total open file descriptors
    int sockets{0};              // TCP/UDP sockets
    int pipes{0};                // Pipes and FIFOs
    int files{0};                // Regular files, directories, device nodes
    int anon_inodes{0};          // eventfd, timerfd, epoll, inotify
    int other{0};                // Unknown or unresolvable
};

struct ProcessStats {
    pid_t pid{0};
    std::string comm;            // Executable name from /proc/[pid]/stat
    int num_threads{0};
    uint64_t utime_ticks{0};     // User CPU time in clock ticks
    uint64_t stime_ticks{0};     // Kernel CPU time in clock ticks
    MemoryStats mem;
    FdStats fds;
};

struct Sample {
    std::chrono::steady_clock::time_point timestamp;
    double elapsed_sec{0.0};
    ProcessStats stats;
    // Aggregated stats of children if follow_children is active
    std::vector<ProcessStats> children;
    ProcessStats aggregate; // Combined target + children
};

struct RegressionResult {
    double slope{0.0};          // Growth per second (bytes/s or fds/s)
    double intercept{0.0};
    double r_squared{0.0};      // Determination coefficient [0.0, 1.0]
    double std_err{0.0};
    uint64_t initial_val{0};
    uint64_t current_val{0};
    int64_t delta_val{0};
    size_t sample_count{0};
};

enum class Severity {
    OK,
    WARMUP,
    SUSPICIOUS,
    LEAK
};

struct LeakVerdict {
    Severity severity{Severity::OK};
    bool is_leaking_memory{false};
    bool is_leaking_fds{false};
    bool exceeded_limits{false};
    std::string memory_reason;
    std::string fd_reason;
    std::string limit_reason;

    RegressionResult anon_regression;
    RegressionResult rss_regression;
    RegressionResult fd_regression;
};

enum class OutputFormat {
    ANSI_TICKER,
    JSON,
    CSV,
    QUIET
};

struct Config {
    pid_t target_pid{-1};
    std::vector<std::string> command;
    double sample_interval_sec{0.5};
    double warmup_sec{3.0};
    double min_r_squared{0.80};
    double mem_leak_threshold_kbs{50.0};     // Alert if sustained growth > 50 KB/s with R² > min_r_squared
    double fd_leak_threshold_per_min{2.0};   // Alert if sustained growth > 2 FDs/min with R² > min_r_squared
    double max_rss_mb{0.0};                  // 0.0 means unconstrained
    double max_anon_mb{0.0};                 // 0.0 means unconstrained
    int max_fds{0};                          // 0 means unconstrained
    bool follow_children{false};
    bool fail_on_leak{false};                // Return exit code 1 if leak detected
    OutputFormat format{OutputFormat::ANSI_TICKER};
    std::string log_file;
};

// Utilities for formatting
std::string format_bytes(uint64_t bytes);
std::string format_rate_kbs(double bytes_per_sec);
std::string format_fd_rate(double fds_per_sec);

} // namespace leakspot

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <sys/types.h>

namespace leakspot {

struct MemoryStats {
    uint64_t rss_bytes{0};          // Resident Set Size (physical memory)
    uint64_t pss_bytes{0};          // Proportional Set Size
    uint64_t anon_bytes{0};         // Anonymous/heap allocations
    uint64_t swap_bytes{0};         // Swapped out pages
    uint64_t swap_pss_bytes{0};     // Proportional swap
    uint64_t shared_clean{0};
    uint64_t shared_dirty{0};
    uint64_t private_clean{0};
    uint64_t private_dirty{0};
    uint64_t referenced_bytes{0};
    uint64_t locked_bytes{0};
    uint64_t vm_size_bytes{0};      // Total virtual memory
    uint64_t vm_peak_bytes{0};      // Peak virtual memory
    uint64_t vm_hwm_bytes{0};       // Peak RSS (High Water Mark)
    uint64_t vm_data_bytes{0};      // Data segment
    uint64_t vm_stk_bytes{0};       // Stack segment
    uint64_t vm_exe_bytes{0};       // Text/code segment
    uint64_t vm_lib_bytes{0};       // Shared library code
    uint64_t vm_pte_bytes{0};       // Page table entries size
    uint64_t minor_faults{0};       // Page reclaims without disk I/O
    uint64_t major_faults{0};       // Page faults requiring disk I/O
};

struct SocketDetails {
    std::string proto;              // "tcp", "tcp6", "udp", "udp6", "unix"
    std::string local_addr;
    int local_port{0};
    std::string remote_addr;
    int remote_port{0};
    std::string state;              // "ESTABLISHED", "LISTEN", "CLOSE_WAIT", etc.
    ino_t inode{0};
};

struct FdStats {
    int total_fds{0};               // Total open file descriptors
    int sockets{0};                 // Sockets total
    int sockets_established{0};
    int sockets_listen{0};
    int sockets_close_wait{0};      // Critical leak indicator
    int sockets_time_wait{0};
    int sockets_other{0};
    int pipes{0};                   // Pipes and FIFOs
    int files{0};                   // Regular files
    int anon_inodes{0};             // eventfd, timerfd, epoll, inotify
    int devices{0};                 // Character/block devices, /dev/null, etc.
    int other{0};                   // Unknown or unresolvable
    std::vector<SocketDetails> active_sockets;
};

struct ThreadStats {
    pid_t tid{0};
    std::string name;
    char state{'R'};
    uint64_t utime_ticks{0};
    uint64_t stime_ticks{0};
    long priority{0};
};

struct ProcessStats {
    pid_t pid{0};
    pid_t ppid{0};
    std::string comm;               // Executable name from /proc/[pid]/stat
    std::string cmdline;            // Full commandline
    char state{'R'};
    int num_threads{0};
    uint64_t utime_ticks{0};        // User CPU time in clock ticks
    uint64_t stime_ticks{0};        // Kernel CPU time in clock ticks
    double cpu_usage_pct{0.0};      // Calculated CPU percentage
    uint64_t voluntary_ctxt_switches{0};
    uint64_t nonvoluntary_ctxt_switches{0};
    MemoryStats mem;
    FdStats fds;
    std::vector<ThreadStats> threads;
};

struct Sample {
    std::chrono::steady_clock::time_point timestamp;
    double elapsed_sec{0.0};
    ProcessStats stats;
    std::vector<ProcessStats> children;
    ProcessStats aggregate;         // Combined parent + children stats
};

struct RegressionResult {
    double slope{0.0};              // Growth rate per second (units/s)
    double intercept{0.0};
    double r_squared{0.0};          // Coefficient of determination [0.0, 1.0]
    double pearson_r{0.0};          // Pearson correlation [-1.0, 1.0]
    double std_err{0.0};            // Standard error of the estimate
    double slope_std_err{0.0};      // Standard error of the slope
    double t_statistic{0.0};        // t-statistic for slope significance
    double confidence_95_low{0.0};  // Lower bound of 95% confidence interval
    double confidence_95_high{0.0}; // Upper bound of 95% confidence interval
    uint64_t initial_val{0};
    uint64_t current_val{0};
    uint64_t peak_val{0};
    uint64_t min_val{0};
    int64_t delta_val{0};
    size_t sample_count{0};
    bool is_statistically_significant{false};
};

struct ExponentialFitResult {
    double a{0.0};                  // y = a * exp(b * x)
    double b{0.0};                  // Growth exponent
    double r_squared{0.0};
    bool is_accelerating{false};
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
    bool is_leaking_threads{false};
    bool is_leaking_sockets_close_wait{false};
    bool exceeded_limits{false};
    double confidence_score{0.0};   // 0.0 to 100.0%

    std::string memory_reason;
    std::string fd_reason;
    std::string thread_reason;
    std::string limit_reason;

    RegressionResult anon_regression;
    RegressionResult rss_regression;
    RegressionResult fd_regression;
    RegressionResult thread_regression;
    ExponentialFitResult anon_exponential;
};

enum class OutputFormat {
    ANSI_TICKER,
    JSON,
    CSV,
    QUIET,
    HTML
};

struct Config {
    pid_t target_pid{-1};
    std::vector<std::string> command;
    std::string working_dir;
    std::map<std::string, std::string> env_vars;
    double sample_interval_sec{0.5};
    double warmup_sec{3.0};
    double timeout_sec{0.0};                 // 0.0 = unlimited
    double min_r_squared{0.80};
    double mem_leak_threshold_kbs{50.0};     // Growth in KB/s
    double fd_leak_threshold_per_min{2.0};   // Growth in FDs/min
    double thread_leak_threshold_per_min{1.0};
    double max_rss_mb{0.0};                  // 0.0 = unconstrained
    double max_anon_mb{0.0};
    int max_fds{0};
    int max_threads{0};
    bool follow_children{false};
    bool fail_on_leak{false};
    bool enable_exponential_check{true};
    bool resolve_socket_endpoints{true};
    bool inspect_maps{false};                // Deep smaps virtual memory address diffing
    OutputFormat format{OutputFormat::ANSI_TICKER};
    std::string log_file;
    std::string html_report_path;
};

// Formatting and utility functions
std::string format_bytes(uint64_t bytes);
std::string format_rate_kbs(double bytes_per_sec);
std::string format_fd_rate(double fds_per_sec);
std::string format_duration(double seconds);
std::string format_number(uint64_t num);
std::string escape_json(const std::string& str);
std::string escape_html(const std::string& str);

} // namespace leakspot

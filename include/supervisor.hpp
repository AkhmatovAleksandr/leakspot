#pragma once

#include "types.hpp"
#include "reporter.hpp"
#include <vector>
#include <atomic>

namespace leakspot {

class Supervisor {
public:
    explicit Supervisor(Config config);
    int run();

private:
    Config config_;
    Reporter reporter_;
    std::vector<Sample> samples_;
    pid_t monitored_pid_{-1};
    bool spawned_child_{false};
    int child_exit_code_{0};

    static inline std::atomic<bool> stop_requested_{false};
    static void signal_handler(int signum);
    void setup_signals();

    bool spawn_target_command();
    bool check_target_alive();
    void sample_cycle(const std::chrono::steady_clock::time_point& start_time);
};

} // namespace leakspot

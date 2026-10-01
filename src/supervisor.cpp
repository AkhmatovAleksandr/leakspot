// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "supervisor.hpp"
#include "sampler.hpp"
#include "detector.hpp"
#include <csignal>
#include <cstring>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <iostream>
#include <format>

namespace leakspot {

void Supervisor::signal_handler(int signum) {
    (void)signum;
    stop_requested_.store(true);
}

Supervisor::Supervisor(Config config)
    : config_(config), reporter_(config_), monitored_pid_(config.target_pid) {}

void Supervisor::setup_signals() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool Supervisor::spawn_target_command() {
    if (config_.command.empty()) return false;

    std::vector<char*> argv_ptrs;
    for (const auto& arg : config_.command) {
        argv_ptrs.push_back(const_cast<char*>(arg.c_str()));
    }
    argv_ptrs.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Failed to fork process: " << strerror(errno) << "\n";
        return false;
    }

    if (pid == 0) {
        // In child process
        if (!config_.working_dir.empty()) {
            if (chdir(config_.working_dir.c_str()) != 0) {
                std::cerr << "Failed to change directory to " << config_.working_dir << ": " << strerror(errno) << "\n";
                _exit(127);
            }
        }

        for (const auto& [k, v] : config_.env_vars) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        execvp(argv_ptrs[0], argv_ptrs.data());
        std::cerr << "Failed to execute '" << argv_ptrs[0] << "': " << strerror(errno) << "\n";
        _exit(127);
    }

    // In parent process
    monitored_pid_ = pid;
    spawned_child_ = true;
    return true;
}

bool Supervisor::check_target_alive() {
    if (spawned_child_) {
        int status = 0;
        pid_t res = waitpid(monitored_pid_, &status, WNOHANG);
        if (res == monitored_pid_) {
            if (WIFEXITED(status)) {
                child_exit_code_ = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                child_exit_code_ = 128 + WTERMSIG(status);
            }
            return false;
        } else if (res < 0) {
            return false;
        }
        return true;
    }

    return Sampler::is_alive(monitored_pid_);
}

void Supervisor::sample_cycle(const std::chrono::steady_clock::time_point& start_time) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start_time).count();

    auto maybe_stats = Sampler::sample_process(monitored_pid_, config_.resolve_socket_endpoints);
    if (!maybe_stats) return;

    Sample sample;
    sample.timestamp = now;
    sample.elapsed_sec = elapsed;
    sample.stats = *maybe_stats;

    if (config_.follow_children) {
        auto child_pids = Sampler::get_child_pids(monitored_pid_);
        for (pid_t cpid : child_pids) {
            auto cstats = Sampler::sample_process(cpid, config_.resolve_socket_endpoints);
            if (cstats) {
                sample.children.push_back(*cstats);
            }
        }
        sample.aggregate = Sampler::aggregate_stats(sample.stats, sample.children);
    } else {
        sample.aggregate = sample.stats;
    }

    samples_.push_back(sample);

    LeakVerdict verdict = Detector::evaluate(samples_, config_);

    reporter_.report_sample(sample, verdict, samples_);
    reporter_.report_alert(verdict);
}

int Supervisor::run() {
    setup_signals();

    if (!config_.command.empty()) {
        if (!spawn_target_command()) {
            return 1;
        }
    } else {
        if (!Sampler::is_alive(monitored_pid_)) {
            std::cerr << std::format("Error: Process with PID {} is not running.\n", monitored_pid_);
            return 1;
        }
    }

    // Capture initial sample for header
    auto initial_stats = Sampler::sample_process(monitored_pid_, false);
    if (!initial_stats) {
        std::cerr << "Failed to read initial process telemetry.\n";
        return 1;
    }

    if (spawned_child_) {
        std::string cmd_name = std::filesystem::path(config_.command[0]).filename().string();
        initial_stats->comm = cmd_name;
    }

    reporter_.print_header(*initial_stats);
    baseline_smaps_ = SmapsInspector::parse_smaps(monitored_pid_);

    auto start_time = std::chrono::steady_clock::now();
    auto interval = std::chrono::duration<double>(config_.sample_interval_sec);

    while (!stop_requested_.load()) {
        if (!check_target_alive()) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if (config_.timeout_sec > 0.0 && elapsed >= config_.timeout_sec) {
            std::cout << std::format("\n[leakspot] Timeout reached ({:.1f}s). Terminating target...\n", config_.timeout_sec);
            if (spawned_child_) {
                kill(monitored_pid_, SIGTERM);
            }
            break;
        }

        sample_cycle(start_time);

        // Keep latest smaps refreshed periodically if leak suspected or inspection requested
        if (config_.inspect_maps || (!samples_.empty() && samples_.back().elapsed_sec >= config_.warmup_sec)) {
            auto current_map = SmapsInspector::parse_smaps(monitored_pid_);
            if (!current_map.empty()) {
                latest_smaps_ = std::move(current_map);
            }
        }

        std::this_thread::sleep_for(interval);
    }

    if (stop_requested_.load() && spawned_child_) {
        kill(monitored_pid_, SIGINT);
        int status = 0;
        waitpid(monitored_pid_, &status, 0);
        if (WIFEXITED(status)) child_exit_code_ = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) child_exit_code_ = 128 + WTERMSIG(status);
    }

    // Final Post-Mortem Audit
    LeakVerdict final_verdict = Detector::evaluate(samples_, config_);
    reporter_.print_summary(samples_, final_verdict);

    if ((config_.inspect_maps || final_verdict.is_leaking_memory) && !baseline_smaps_.empty() && !latest_smaps_.empty()) {
        auto diffs = SmapsInspector::diff_mappings(baseline_smaps_, latest_smaps_, 5);
        std::cout << "\n" << SmapsInspector::format_diff_report(diffs) << "\n";
    }

    if (config_.fail_on_leak && final_verdict.severity == Severity::LEAK) {
        return 1;
    }

    if (spawned_child_) {
        return child_exit_code_;
    }

    return 0;
}

} // namespace leakspot

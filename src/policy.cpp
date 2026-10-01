// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "policy.hpp"
#include "regression.hpp"
#include <format>
#include <algorithm>

namespace leakspot {

RuleResult PolicyEngine::check_memory_linear(const RegressionResult& reg, const Config& config) {
    RuleResult res;
    res.rule_name = "MemoryLinearSlope";
    if (reg.sample_count < 3) return res;

    double kbs = reg.slope / 1024.0;
    bool positive_delta = reg.delta_val > 0;
    bool high_r2 = reg.r_squared >= config.min_r_squared;

    if (kbs >= config.mem_leak_threshold_kbs && high_r2 && positive_delta) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = reg.r_squared * 100.0;
        res.description = std::format(
            "Persistent memory leak: growing at +{:.1f} KB/s (R²={:.2f}, Δ={})",
            kbs, reg.r_squared, format_bytes(static_cast<uint64_t>(reg.delta_val))
        );
    } else if (kbs >= config.mem_leak_threshold_kbs * 0.5 && reg.r_squared >= 0.50 && positive_delta) {
        res.triggered = true;
        res.severity = Severity::SUSPICIOUS;
        res.confidence = reg.r_squared * 70.0;
        res.description = std::format(
            "Suspicious memory trend: +{:.1f} KB/s with moderate correlation (R²={:.2f})",
            kbs, reg.r_squared
        );
    }

    return res;
}

RuleResult PolicyEngine::check_memory_exponential(const ExponentialFitResult& exp_fit, const Config& config) {
    RuleResult res;
    res.rule_name = "MemoryExponentialAcceleration";
    if (!config.enable_exponential_check) return res;

    if (exp_fit.is_accelerating) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = exp_fit.r_squared * 95.0;
        res.description = std::format(
            "Accelerating non-linear memory leak detected (growth exponent b={:.3f}, R²={:.2f})",
            exp_fit.b, exp_fit.r_squared
        );
    }

    return res;
}

RuleResult PolicyEngine::check_fd_growth(const RegressionResult& reg, const Config& config) {
    RuleResult res;
    res.rule_name = "FdHandleGrowth";
    if (reg.sample_count < 3) return res;

    double fds_per_min = reg.slope * 60.0;
    bool positive_delta = reg.delta_val > 0;
    bool high_r2 = reg.r_squared >= config.min_r_squared;

    if (fds_per_min >= config.fd_leak_threshold_per_min && high_r2 && positive_delta) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = reg.r_squared * 100.0;
        res.description = std::format(
            "Unclosed file descriptors leak: growing at +{:.1f} FDs/min (R²={:.2f}, Δ=+{} handles)",
            fds_per_min, reg.r_squared, reg.delta_val
        );
    } else if (fds_per_min >= config.fd_leak_threshold_per_min * 0.5 && reg.r_squared >= 0.50 && positive_delta) {
        res.triggered = true;
        res.severity = Severity::SUSPICIOUS;
        res.confidence = reg.r_squared * 70.0;
        res.description = std::format(
            "Suspicious file descriptor trend: +{:.1f} FDs/min", fds_per_min
        );
    }

    return res;
}

RuleResult PolicyEngine::check_close_wait_sockets(const std::vector<Sample>& samples) {
    RuleResult res;
    res.rule_name = "SocketCloseWaitAccumulation";
    if (samples.size() < 3) return res;

    int initial_cw = samples.front().aggregate.fds.sockets_close_wait;
    int current_cw = samples.back().aggregate.fds.sockets_close_wait;
    int delta = current_cw - initial_cw;

    if (current_cw >= 5 && delta >= 3) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = 90.0;
        res.description = std::format(
            "Socket leak: {} sockets stuck in CLOSE_WAIT state (unclosed network connections)",
            current_cw
        );
    }

    return res;
}

RuleResult PolicyEngine::check_thread_leak(const RegressionResult& reg, const Config& config) {
    RuleResult res;
    res.rule_name = "ThreadLeak";
    if (reg.sample_count < 3) return res;

    double th_per_min = reg.slope * 60.0;
    if (th_per_min >= config.thread_leak_threshold_per_min && reg.r_squared >= config.min_r_squared && reg.delta_val >= 2) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = reg.r_squared * 90.0;
        res.description = std::format(
            "Thread leak: thread count increasing at +{:.1f} threads/min without joining (Δ=+{} threads)",
            th_per_min, reg.delta_val
        );
    }

    return res;
}

RuleResult PolicyEngine::check_hard_limits(const ProcessStats& current, const Config& config) {
    RuleResult res;
    res.rule_name = "HardLimitCeiling";

    double current_rss_mb = static_cast<double>(current.mem.rss_bytes) / (1024.0 * 1024.0);
    double current_anon_mb = static_cast<double>(current.mem.anon_bytes) / (1024.0 * 1024.0);

    if (config.max_rss_mb > 0.0 && current_rss_mb > config.max_rss_mb) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = 100.0;
        res.description = std::format("RSS memory ({:.1f} MB) exceeded ceiling of {:.1f} MB", current_rss_mb, config.max_rss_mb);
    } else if (config.max_anon_mb > 0.0 && current_anon_mb > config.max_anon_mb) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = 100.0;
        res.description = std::format("Anonymous memory ({:.1f} MB) exceeded ceiling of {:.1f} MB", current_anon_mb, config.max_anon_mb);
    } else if (config.max_fds > 0 && current.fds.total_fds > config.max_fds) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = 100.0;
        res.description = std::format("Open file descriptors ({}) exceeded ceiling of {}", current.fds.total_fds, config.max_fds);
    } else if (config.max_threads > 0 && current.num_threads > config.max_threads) {
        res.triggered = true;
        res.severity = Severity::LEAK;
        res.confidence = 100.0;
        res.description = std::format("Active threads ({}) exceeded ceiling of {}", current.num_threads, config.max_threads);
    }

    return res;
}

std::vector<RuleResult> PolicyEngine::evaluate_all(const std::vector<Sample>& samples, const Config& config) {
    std::vector<RuleResult> results;
    if (samples.empty()) return results;

    const auto& last = samples.back();
    if (last.elapsed_sec < config.warmup_sec) {
        RuleResult w;
        w.rule_name = "WarmupGracePeriod";
        w.severity = Severity::WARMUP;
        w.description = std::format("In warmup grace period ({:.1f}s / {:.1f}s)", last.elapsed_sec, config.warmup_sec);
        results.push_back(w);
        return results;
    }

    auto anon_reg = Regression::compute_memory_trend(samples, config.warmup_sec, true);
    auto exp_fit = Regression::compute_memory_exponential(samples, config.warmup_sec);
    auto fd_reg = Regression::compute_fd_trend(samples, config.warmup_sec);
    auto th_reg = Regression::compute_thread_trend(samples, config.warmup_sec);

    results.push_back(check_memory_linear(anon_reg, config));
    results.push_back(check_memory_exponential(exp_fit, config));
    results.push_back(check_fd_growth(fd_reg, config));
    results.push_back(check_close_wait_sockets(samples));
    results.push_back(check_thread_leak(th_reg, config));
    results.push_back(check_hard_limits(last.aggregate, config));

    return results;
}

LeakVerdict PolicyEngine::compile_verdict(const std::vector<RuleResult>& rule_results, const std::vector<Sample>& samples, const Config& config) {
    LeakVerdict verdict;
    if (samples.empty()) {
        verdict.severity = Severity::WARMUP;
        return verdict;
    }

    verdict.anon_regression = Regression::compute_memory_trend(samples, config.warmup_sec, true);
    verdict.rss_regression = Regression::compute_rss_trend(samples, config.warmup_sec);
    verdict.fd_regression = Regression::compute_fd_trend(samples, config.warmup_sec);
    verdict.thread_regression = Regression::compute_thread_trend(samples, config.warmup_sec);
    verdict.anon_exponential = Regression::compute_memory_exponential(samples, config.warmup_sec);

    if (samples.back().elapsed_sec < config.warmup_sec || verdict.anon_regression.sample_count < 3) {
        verdict.severity = Severity::WARMUP;
        return verdict;
    }

    double max_conf = 0.0;
    bool has_leak = false;
    bool has_suspicious = false;

    for (const auto& r : rule_results) {
        if (!r.triggered) continue;

        max_conf = std::max(max_conf, r.confidence);

        if (r.severity == Severity::LEAK) {
            has_leak = true;
            if (r.rule_name == "MemoryLinearSlope" || r.rule_name == "MemoryExponentialAcceleration") {
                verdict.is_leaking_memory = true;
                verdict.memory_reason = r.description;
            } else if (r.rule_name == "FdHandleGrowth") {
                verdict.is_leaking_fds = true;
                verdict.fd_reason = r.description;
            } else if (r.rule_name == "SocketCloseWaitAccumulation") {
                verdict.is_leaking_sockets_close_wait = true;
                verdict.fd_reason += " | " + r.description;
            } else if (r.rule_name == "ThreadLeak") {
                verdict.is_leaking_threads = true;
                verdict.thread_reason = r.description;
            } else if (r.rule_name == "HardLimitCeiling") {
                verdict.exceeded_limits = true;
                verdict.limit_reason = r.description;
            }
        } else if (r.severity == Severity::SUSPICIOUS) {
            has_suspicious = true;
            if (verdict.memory_reason.empty() && r.rule_name == "MemoryLinearSlope") {
                verdict.memory_reason = r.description;
            }
            if (verdict.fd_reason.empty() && r.rule_name == "FdHandleGrowth") {
                verdict.fd_reason = r.description;
            }
        }
    }

    verdict.confidence_score = max_conf;

    if (has_leak) {
        verdict.severity = Severity::LEAK;
    } else if (has_suspicious) {
        verdict.severity = Severity::SUSPICIOUS;
    } else {
        verdict.severity = Severity::OK;
    }

    return verdict;
}

} // namespace leakspot

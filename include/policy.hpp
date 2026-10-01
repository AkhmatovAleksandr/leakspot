// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include "types.hpp"
#include <vector>
#include <string>

namespace leakspot {

struct RuleResult {
    std::string rule_name;
    bool triggered{false};
    Severity severity{Severity::OK};
    std::string description;
    double confidence{0.0};
};

class PolicyEngine {
public:
    static std::vector<RuleResult> evaluate_all(const std::vector<Sample>& samples, const Config& config);
    static LeakVerdict compile_verdict(const std::vector<RuleResult>& rule_results, const std::vector<Sample>& samples, const Config& config);

private:
    static RuleResult check_memory_linear(const RegressionResult& reg, const Config& config);
    static RuleResult check_memory_exponential(const ExponentialFitResult& exp_fit, const Config& config);
    static RuleResult check_fd_growth(const RegressionResult& reg, const Config& config);
    static RuleResult check_close_wait_sockets(const std::vector<Sample>& samples);
    static RuleResult check_thread_leak(const RegressionResult& reg, const Config& config);
    static RuleResult check_hard_limits(const ProcessStats& current, const Config& config);
};

} // namespace leakspot

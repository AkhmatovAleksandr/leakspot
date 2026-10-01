// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include "types.hpp"
#include <vector>
#include <utility>

namespace leakspot {

class Regression {
public:
    static RegressionResult compute(const std::vector<std::pair<double, uint64_t>>& points);
    static ExponentialFitResult compute_exponential(const std::vector<std::pair<double, uint64_t>>& points);

    static RegressionResult compute_memory_trend(const std::vector<Sample>& samples, double warmup_sec, bool anon_only = true);
    static RegressionResult compute_rss_trend(const std::vector<Sample>& samples, double warmup_sec);
    static RegressionResult compute_fd_trend(const std::vector<Sample>& samples, double warmup_sec);
    static RegressionResult compute_thread_trend(const std::vector<Sample>& samples, double warmup_sec);

    static ExponentialFitResult compute_memory_exponential(const std::vector<Sample>& samples, double warmup_sec);
};

} // namespace leakspot

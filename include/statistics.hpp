// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <utility>

namespace leakspot {

struct DescriptiveStats {
    double count{0.0};
    double mean{0.0};
    double median{0.0};
    double variance{0.0};
    double std_dev{0.0};
    double min_val{0.0};
    double max_val{0.0};
    double p50{0.0};
    double p90{0.0};
    double p95{0.0};
    double p99{0.0};
};

class Statistics {
public:
    static DescriptiveStats summarize(const std::vector<double>& values);
    static double mean(const std::vector<double>& values);
    static double median(std::vector<double> values);
    static double variance(const std::vector<double>& values);
    static double std_dev(const std::vector<double>& values);
    static double percentile(std::vector<double> values, double p); // p in [0.0, 1.0]

    // Moving Averages
    static std::vector<double> simple_moving_average(const std::vector<double>& values, size_t window);
    static std::vector<double> exponential_moving_average(const std::vector<double>& values, double alpha);

    // Robust Statistics & Outlier Filtering
    static double median_absolute_deviation(const std::vector<double>& values);
    static std::vector<std::pair<double, uint64_t>> filter_outliers_mad(
        const std::vector<std::pair<double, uint64_t>>& points,
        double threshold = 3.5
    );

    // Robust Theil-Sen Estimator (slope unaffected by up to 29% outliers)
    static double theil_sen_slope(const std::vector<std::pair<double, uint64_t>>& points);

    // Student's t approximation for p-value estimation
    static double t_to_p_value(double t, double df);
};

} // namespace leakspot

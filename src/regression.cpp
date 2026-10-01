// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "regression.hpp"
#include "statistics.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace leakspot {

RegressionResult Regression::compute(const std::vector<std::pair<double, uint64_t>>& points) {
    RegressionResult res;
    res.sample_count = points.size();

    if (points.empty()) {
        return res;
    }

    res.initial_val = points.front().second;
    res.current_val = points.back().second;
    res.delta_val = static_cast<int64_t>(res.current_val) - static_cast<int64_t>(res.initial_val);

    uint64_t min_v = points.front().second;
    uint64_t max_v = points.front().second;
    for (const auto& [_, y] : points) {
        min_v = std::min(min_v, y);
        max_v = std::max(max_v, y);
    }
    res.min_val = min_v;
    res.peak_val = max_v;

    if (points.size() < 2) {
        return res;
    }

    double n = static_cast<double>(points.size());
    double sum_x = 0.0;
    double sum_y = 0.0;

    for (const auto& [x, y] : points) {
        sum_x += x;
        sum_y += static_cast<double>(y);
    }

    double mean_x = sum_x / n;
    double mean_y = sum_y / n;

    double ss_xx = 0.0;
    double ss_yy = 0.0;
    double ss_xy = 0.0;

    for (const auto& [x, y] : points) {
        double dx = x - mean_x;
        double dy = static_cast<double>(y) - mean_y;
        ss_xx += dx * dx;
        ss_yy += dy * dy;
        ss_xy += dx * dy;
    }

    if (ss_xx <= 1e-9) {
        // All timestamps identical
        return res;
    }

    res.slope = ss_xy / ss_xx;
    res.intercept = mean_y - res.slope * mean_x;

    if (ss_yy <= 1e-9) {
        // Completely flat line (y doesn't change at all)
        res.r_squared = 1.0;
        res.pearson_r = 0.0;
        res.std_err = 0.0;
        res.slope_std_err = 0.0;
        res.confidence_95_low = res.slope;
        res.confidence_95_high = res.slope;
        return res;
    }

    double r = ss_xy / std::sqrt(ss_xx * ss_yy);
    res.pearson_r = std::clamp(r, -1.0, 1.0);
    res.r_squared = std::clamp(r * r, 0.0, 1.0);

    // Sum of squared residuals
    double ss_res = 0.0;
    for (const auto& [x, y] : points) {
        double y_pred = res.slope * x + res.intercept;
        double err = static_cast<double>(y) - y_pred;
        ss_res += err * err;
    }

    if (n > 2.0) {
        double df = n - 2.0;
        res.std_err = std::sqrt(ss_res / df);
        res.slope_std_err = res.std_err / std::sqrt(ss_xx);

        if (res.slope_std_err > 1e-9) {
            res.t_statistic = res.slope / res.slope_std_err;
            double p_val = Statistics::t_to_p_value(res.t_statistic, df);
            res.is_statistically_significant = (p_val < 0.05);

            // 95% confidence interval: critical t approx 1.96 for large df, higher for small df
            double t_crit = (df >= 30.0) ? 1.96 : (2.0 + 4.0 / df);
            res.confidence_95_low = res.slope - t_crit * res.slope_std_err;
            res.confidence_95_high = res.slope + t_crit * res.slope_std_err;
        }
    }

    return res;
}

ExponentialFitResult Regression::compute_exponential(const std::vector<std::pair<double, uint64_t>>& points) {
    ExponentialFitResult res;
    if (points.size() < 3) return res;

    // Linearize y = a * exp(b * x) => ln(y) = ln(a) + b * x
    std::vector<std::pair<double, uint64_t>> log_points;
    log_points.reserve(points.size());

    for (const auto& [x, y] : points) {
        if (y <= 0) return res; // Cannot take log of non-positive
        // Convert to fixed-point scaled log
        double log_y = std::log(static_cast<double>(y));
        log_points.emplace_back(x, static_cast<uint64_t>(log_y * 1000.0));
    }

    auto lin = compute(log_points);
    res.b = lin.slope / 1000.0;
    res.a = std::exp(lin.intercept / 1000.0);
    res.r_squared = lin.r_squared;

    // Check if the actual rate of growth (dy/dx) is accelerating
    double start_rate = (points[1].first > points[0].first)
        ? static_cast<double>(points[1].second - points[0].second) / (points[1].first - points[0].first)
        : 0.0;
    size_t last = points.size() - 1;
    double end_rate = (points[last].first > points[last - 1].first)
        ? static_cast<double>(points[last].second - points[last - 1].second) / (points[last].first - points[last - 1].first)
        : 0.0;

    bool rate_increasing = (end_rate > start_rate * 1.25) && (end_rate > start_rate + 5.0);

    res.is_accelerating = (res.b > 0.05 && res.r_squared >= 0.85 && rate_increasing);

    return res;
}

RegressionResult Regression::compute_memory_trend(const std::vector<Sample>& samples, double warmup_sec, bool anon_only) {
    std::vector<std::pair<double, uint64_t>> points;
    points.reserve(samples.size());

    for (const auto& s : samples) {
        if (s.elapsed_sec >= warmup_sec) {
            uint64_t val = anon_only ? s.aggregate.mem.anon_bytes : s.aggregate.mem.rss_bytes;
            points.emplace_back(s.elapsed_sec, val);
        }
    }

    return compute(points);
}

RegressionResult Regression::compute_rss_trend(const std::vector<Sample>& samples, double warmup_sec) {
    return compute_memory_trend(samples, warmup_sec, false);
}

RegressionResult Regression::compute_fd_trend(const std::vector<Sample>& samples, double warmup_sec) {
    std::vector<std::pair<double, uint64_t>> points;
    points.reserve(samples.size());

    for (const auto& s : samples) {
        if (s.elapsed_sec >= warmup_sec) {
            points.emplace_back(s.elapsed_sec, static_cast<uint64_t>(s.aggregate.fds.total_fds));
        }
    }

    return compute(points);
}

RegressionResult Regression::compute_thread_trend(const std::vector<Sample>& samples, double warmup_sec) {
    std::vector<std::pair<double, uint64_t>> points;
    points.reserve(samples.size());

    for (const auto& s : samples) {
        if (s.elapsed_sec >= warmup_sec) {
            points.emplace_back(s.elapsed_sec, static_cast<uint64_t>(s.aggregate.num_threads));
        }
    }

    return compute(points);
}

ExponentialFitResult Regression::compute_memory_exponential(const std::vector<Sample>& samples, double warmup_sec) {
    std::vector<std::pair<double, uint64_t>> points;
    points.reserve(samples.size());

    for (const auto& s : samples) {
        if (s.elapsed_sec >= warmup_sec) {
            points.emplace_back(s.elapsed_sec, s.aggregate.mem.anon_bytes);
        }
    }

    return compute_exponential(points);
}

} // namespace leakspot

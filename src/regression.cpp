#include "regression.hpp"
#include <cmath>
#include <numeric>

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
        // All x points are identical (no elapsed time)
        return res;
    }

    res.slope = ss_xy / ss_xx;
    res.intercept = mean_y - res.slope * mean_x;

    if (ss_yy <= 1e-9) {
        // All y values are identical (flat, completely constant)
        res.r_squared = 1.0;
        res.std_err = 0.0;
    } else {
        double r = ss_xy / std::sqrt(ss_xx * ss_yy);
        res.r_squared = std::clamp(r * r, 0.0, 1.0);

        // Standard error of the estimate
        double ss_res = 0.0;
        for (const auto& [x, y] : points) {
            double y_pred = res.slope * x + res.intercept;
            double err = static_cast<double>(y) - y_pred;
            ss_res += err * err;
        }
        if (n > 2.0) {
            res.std_err = std::sqrt(ss_res / (n - 2.0));
        }
    }

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

} // namespace leakspot

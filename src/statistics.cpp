// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "statistics.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace leakspot {

double Statistics::mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double Statistics::median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    size_t n = values.size();
    std::sort(values.begin(), values.end());
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return (values[(n / 2) - 1] + values[n / 2]) / 2.0;
}

double Statistics::variance(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double m = mean(values);
    double accum = 0.0;
    for (double val : values) {
        double d = val - m;
        accum += d * d;
    }
    return accum / static_cast<double>(values.size() - 1);
}

double Statistics::std_dev(const std::vector<double>& values) {
    return std::sqrt(variance(values));
}

double Statistics::percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    p = std::clamp(p, 0.0, 1.0);
    std::sort(values.begin(), values.end());
    if (p == 0.0) return values.front();
    if (p == 1.0) return values.back();

    double rank = p * static_cast<double>(values.size() - 1);
    size_t low = static_cast<size_t>(std::floor(rank));
    size_t high = static_cast<size_t>(std::ceil(rank));
    double weight = rank - static_cast<double>(low);

    return (1.0 - weight) * values[low] + weight * values[high];
}

DescriptiveStats Statistics::summarize(const std::vector<double>& values) {
    DescriptiveStats stats;
    if (values.empty()) return stats;

    stats.count = static_cast<double>(values.size());
    stats.mean = mean(values);
    stats.median = median(values);
    stats.variance = variance(values);
    stats.std_dev = std::sqrt(stats.variance);

    auto min_max = std::minmax_element(values.begin(), values.end());
    stats.min_val = *min_max.first;
    stats.max_val = *min_max.second;

    stats.p50 = percentile(values, 0.50);
    stats.p90 = percentile(values, 0.90);
    stats.p95 = percentile(values, 0.95);
    stats.p99 = percentile(values, 0.99);

    return stats;
}

std::vector<double> Statistics::simple_moving_average(const std::vector<double>& values, size_t window) {
    std::vector<double> result;
    if (values.empty() || window == 0) return result;
    result.reserve(values.size());

    double sum = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (i >= window) {
            sum -= values[i - window];
            result.push_back(sum / static_cast<double>(window));
        } else {
            result.push_back(sum / static_cast<double>(i + 1));
        }
    }
    return result;
}

std::vector<double> Statistics::exponential_moving_average(const std::vector<double>& values, double alpha) {
    std::vector<double> result;
    if (values.empty()) return result;
    result.reserve(values.size());

    alpha = std::clamp(alpha, 0.01, 1.0);
    double ema = values.front();
    result.push_back(ema);

    for (size_t i = 1; i < values.size(); ++i) {
        ema = alpha * values[i] + (1.0 - alpha) * ema;
        result.push_back(ema);
    }
    return result;
}

double Statistics::median_absolute_deviation(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double med = median(values);
    std::vector<double> abs_diffs;
    abs_diffs.reserve(values.size());
    for (double v : values) {
        abs_diffs.push_back(std::abs(v - med));
    }
    return median(abs_diffs);
}

std::vector<std::pair<double, uint64_t>> Statistics::filter_outliers_mad(
    const std::vector<std::pair<double, uint64_t>>& points,
    double threshold
) {
    if (points.size() < 5) return points;

    std::vector<double> y_vals;
    y_vals.reserve(points.size());
    for (const auto& [_, y] : points) {
        y_vals.push_back(static_cast<double>(y));
    }

    double med = median(y_vals);
    double mad = median_absolute_deviation(y_vals);
    if (mad <= 1e-9) return points;

    std::vector<std::pair<double, uint64_t>> filtered;
    filtered.reserve(points.size());

    for (const auto& pt : points) {
        double score = 0.6745 * std::abs(static_cast<double>(pt.second) - med) / mad;
        if (score <= threshold) {
            filtered.push_back(pt);
        }
    }

    return filtered.empty() ? points : filtered;
}

double Statistics::theil_sen_slope(const std::vector<std::pair<double, uint64_t>>& points) {
    if (points.size() < 2) return 0.0;

    std::vector<double> slopes;
    slopes.reserve((points.size() * (points.size() - 1)) / 2);

    for (size_t i = 0; i < points.size(); ++i) {
        for (size_t j = i + 1; j < points.size(); ++j) {
            double dx = points[j].first - points[i].first;
            if (std::abs(dx) > 1e-9) {
                double dy = static_cast<double>(points[j].second) - static_cast<double>(points[i].second);
                slopes.push_back(dy / dx);
            }
        }
    }

    if (slopes.empty()) return 0.0;
    return median(slopes);
}

double Statistics::t_to_p_value(double t, double df) {
    if (df <= 0.0) return 1.0;
    t = std::abs(t);

    // Hill's polynomial approximation for Student's t distribution two-tailed p-value
    double a = df / (df + t * t);
    double x = 1.0 - a;
    if (x <= 0.0) return 1.0;
    if (x >= 1.0) return 0.0;

    // Fast approximation based on standard normal transform
    double z = (t * (1.0 - 1.0 / (4.0 * df))) / std::sqrt(1.0 + (t * t) / (2.0 * df));
    // Standard normal CDF complementary error function approximation
    double p = 0.5 * std::erfc(z / std::sqrt(2.0)) * 2.0;
    return std::clamp(p, 0.0, 1.0);
}

} // namespace leakspot

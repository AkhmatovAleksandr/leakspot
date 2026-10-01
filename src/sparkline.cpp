#include "sparkline.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace leakspot {

static const char* UNICODE_TICKS[] = {
    " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"
};

static const char ASCII_TICKS[] = {
    '_', '.', '-', '=', '+', '*', '#', '%'
};

std::string Sparkline::render_unicode(const std::vector<double>& values, size_t max_width) {
    if (values.empty()) return "";

    std::vector<double> downsampled;
    if (values.size() > max_width && max_width > 0) {
        downsampled.reserve(max_width);
        double step = static_cast<double>(values.size()) / static_cast<double>(max_width);
        for (size_t i = 0; i < max_width; ++i) {
            size_t idx = std::min(static_cast<size_t>(static_cast<double>(i) * step), values.size() - 1);
            downsampled.push_back(values[idx]);
        }
    } else {
        downsampled = values;
    }

    auto min_max = std::minmax_element(downsampled.begin(), downsampled.end());
    double min_v = *min_max.first;
    double max_v = *min_max.second;
    double range = max_v - min_v;

    std::string out;
    out.reserve(downsampled.size() * 4);

    for (double v : downsampled) {
        if (range <= 1e-9) {
            out += UNICODE_TICKS[0];
        } else {
            double normalized = (v - min_v) / range;
            int bin = std::clamp(static_cast<int>(normalized * 7.99), 0, 7);
            out += UNICODE_TICKS[bin];
        }
    }

    return out;
}

std::string Sparkline::render_ascii(const std::vector<double>& values, size_t max_width) {
    if (values.empty()) return "";

    std::vector<double> downsampled;
    if (values.size() > max_width && max_width > 0) {
        downsampled.reserve(max_width);
        double step = static_cast<double>(values.size()) / static_cast<double>(max_width);
        for (size_t i = 0; i < max_width; ++i) {
            size_t idx = std::min(static_cast<size_t>(static_cast<double>(i) * step), values.size() - 1);
            downsampled.push_back(values[idx]);
        }
    } else {
        downsampled = values;
    }

    auto min_max = std::minmax_element(downsampled.begin(), downsampled.end());
    double min_v = *min_max.first;
    double max_v = *min_max.second;
    double range = max_v - min_v;

    std::string out;
    out.reserve(downsampled.size());

    for (double v : downsampled) {
        if (range <= 1e-9) {
            out += ASCII_TICKS[0];
        } else {
            double normalized = (v - min_v) / range;
            int bin = std::clamp(static_cast<int>(normalized * 7.99), 0, 7);
            out += ASCII_TICKS[bin];
        }
    }

    return out;
}

std::vector<std::string> Sparkline::render_chart(
    const std::vector<double>& values,
    size_t width,
    size_t height,
    const std::string& label
) {
    std::vector<std::string> lines;
    if (values.empty() || width == 0 || height == 0) return lines;

    std::vector<double> sample_pts;
    sample_pts.reserve(width);
    double step = static_cast<double>(values.size()) / static_cast<double>(width);
    for (size_t i = 0; i < width; ++i) {
        size_t idx = std::min(static_cast<size_t>(static_cast<double>(i) * step), values.size() - 1);
        sample_pts.push_back(values[idx]);
    }

    auto min_max = std::minmax_element(sample_pts.begin(), sample_pts.end());
    double min_v = *min_max.first;
    double max_v = *min_max.second;
    double range = max_v - min_v;

    lines.resize(height);

    for (size_t r = 0; r < height; ++r) {
        std::string row_str;
        row_str.reserve(width);

        for (size_t c = 0; c < width; ++c) {
            double val = sample_pts[c];
            if (range <= 1e-9) {
                row_str += (r == height - 1) ? "-" : " ";
            } else {
                double normalized_y = (val - min_v) / range; // 0 to 1
                double cell_y = static_cast<double>(height - 1 - r) / static_cast<double>(height - 1);
                if (std::abs(normalized_y - cell_y) < (0.5 / static_cast<double>(height))) {
                    row_str += "*";
                } else if (normalized_y >= cell_y) {
                    row_str += "|";
                } else {
                    row_str += " ";
                }
            }
        }

        std::string axis_label;
        if (r == 0) axis_label = std::format("{:>8.1f} |", max_v);
        else if (r == height - 1) axis_label = std::format("{:>8.1f} |", min_v);
        else axis_label = "         |";

        lines[r] = axis_label + row_str;
    }

    if (!label.empty()) {
        std::string bottom = "         +" + std::string(width, '-') + " " + label;
        lines.push_back(bottom);
    }

    return lines;
}

} // namespace leakspot

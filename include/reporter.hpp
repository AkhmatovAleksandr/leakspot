// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <memory>

namespace leakspot {

class Reporter {
public:
    explicit Reporter(const Config& config);
    ~Reporter();

    void print_header(const ProcessStats& initial);
    void report_sample(const Sample& sample, const LeakVerdict& verdict, const std::vector<Sample>& all_samples);
    void report_alert(const LeakVerdict& verdict);
    void print_summary(const std::vector<Sample>& samples, const LeakVerdict& final_verdict);
    void generate_html_report(const std::vector<Sample>& samples, const LeakVerdict& final_verdict, const std::string& path);

private:
    Config config_;
    std::unique_ptr<std::ofstream> log_stream_;
    bool has_alerted_mem_{false};
    bool has_alerted_fd_{false};
    bool has_alerted_thread_{false};
    bool has_alerted_limit_{false};

    void output_line(const std::string& line);
    std::string format_ansi_row(const Sample& sample, const LeakVerdict& verdict, const std::vector<Sample>& all_samples);
    std::string format_json_sample(const Sample& sample, const LeakVerdict& verdict);
    std::string format_csv_row(const Sample& sample, const LeakVerdict& verdict);
};

} // namespace leakspot

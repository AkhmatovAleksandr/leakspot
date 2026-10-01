// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "types.hpp"
#include "statistics.hpp"
#include "regression.hpp"
#include "procfs.hpp"
#include "sparkline.hpp"
#include "policy.hpp"
#include "detector.hpp"
#include "cli.hpp"
#include "sampler.hpp"
#include "smaps.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cassert>
#include <format>

using namespace leakspot;

// Simple, zero-dependency unit test runner
namespace test_framework {
    static int total_tests = 0;
    static int passed_tests = 0;
    static int failed_tests = 0;

    void run_test(const std::string& name, bool condition, const std::string& detail = "") {
        ++total_tests;
        if (condition) {
            ++passed_tests;
            std::cout << std::format("  [PASS] {:03d}: {}\n", total_tests, name);
        } else {
            ++failed_tests;
            std::cerr << std::format("  [FAIL] {:03d}: {} -> {}\n", total_tests, name, detail);
        }
    }
}

#define TEST_ASSERT(name, cond) test_framework::run_test(name, (cond), #cond)
#define TEST_ASSERT_NEAR(name, val, expected, eps) \
    test_framework::run_test(name, (std::abs((val) - (expected)) <= (eps)), \
        std::format("expected ~{:.4f}, got {:.4f}", static_cast<double>(expected), static_cast<double>(val)))

int main() {
    std::cout << "================================================================================\n";
    std::cout << "              LEAKSPOT COMPREHENSIVE TEST SUITE (100+ TESTS)                    \n";
    std::cout << "================================================================================\n\n";

    // -------------------------------------------------------------------------
    // CATEGORY 1: Types & Formatting Utilities (Tests 1 - 20)
    // -------------------------------------------------------------------------
    std::cout << "--- Category 1: Types & Formatting Utilities ---\n";

    TEST_ASSERT("Format bytes: 0 bytes", format_bytes(0) == "0 B");
    TEST_ASSERT("Format bytes: 512 bytes", format_bytes(512) == "512 B");
    TEST_ASSERT("Format bytes: 1023 bytes", format_bytes(1023) == "1023 B");
    TEST_ASSERT("Format bytes: exact 1 KB", format_bytes(1024) == "1.0 KB");
    TEST_ASSERT("Format bytes: 1.5 KB", format_bytes(1536) == "1.5 KB");
    TEST_ASSERT("Format bytes: exact 1 MB", format_bytes(1024 * 1024) == "1.0 MB");
    TEST_ASSERT("Format bytes: 100 MB", format_bytes(100ULL * 1024 * 1024) == "100.0 MB");
    TEST_ASSERT("Format bytes: exact 1 GB", format_bytes(1024ULL * 1024 * 1024) == "1.00 GB");
    TEST_ASSERT("Format bytes: 2.5 GB", format_bytes(2684354560ULL) == "2.50 GB");
    TEST_ASSERT("Format bytes: exact 1 TB", format_bytes(1024ULL * 1024 * 1024 * 1024) == "1.00 TB");

    TEST_ASSERT("Format rate: zero bytes/s", format_rate_kbs(0.0) == "+0.0 KB/s");
    TEST_ASSERT("Format rate: +50 KB/s", format_rate_kbs(50.0 * 1024.0) == "+50.0 KB/s");
    TEST_ASSERT("Format rate: -20 KB/s", format_rate_kbs(-20.0 * 1024.0) == "-20.0 KB/s");
    TEST_ASSERT("Format rate: +5 MB/s", format_rate_kbs(5.0 * 1024.0 * 1024.0) == "+5.00 MB/s");
    TEST_ASSERT("Format rate: +2 GB/s", format_rate_kbs(2.0 * 1024.0 * 1024.0 * 1024.0) == "+2.00 GB/s");

    TEST_ASSERT("Format FD rate: 0/min", format_fd_rate(0.0) == "+0.0 FDs/min");
    TEST_ASSERT("Format FD rate: +6 FDs/min", format_fd_rate(0.1) == "+6.0 FDs/min");
    TEST_ASSERT("Format duration: 0.0s", format_duration(0.0) == "00:00.0");
    TEST_ASSERT("Format duration: 65.5s", format_duration(65.5) == "01:05.5");
    TEST_ASSERT("Format duration: 3661.2s", format_duration(3661.2) == "01:01:01.2");

    // -------------------------------------------------------------------------
    // CATEGORY 2: Number Formatting & Sanitization (Tests 21 - 30)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 2: Number Formatting & Sanitization ---\n";

    TEST_ASSERT("Format number: single digit", format_number(7) == "7");
    TEST_ASSERT("Format number: hundreds", format_number(999) == "999");
    TEST_ASSERT("Format number: thousands", format_number(1000) == "1,000");
    TEST_ASSERT("Format number: millions", format_number(1234567) == "1,234,567");
    TEST_ASSERT("Format number: billions", format_number(1000000000ULL) == "1,000,000,000");

    TEST_ASSERT("Escape JSON: empty string", escape_json("") == "");
    TEST_ASSERT("Escape JSON: normal string", escape_json("hello world") == "hello world");
    TEST_ASSERT("Escape JSON: quotes and backslashes", escape_json("quote \" and \\") == "quote \\\" and \\\\");
    TEST_ASSERT("Escape JSON: newlines and tabs", escape_json("line1\nline2\ttab") == "line1\\nline2\\ttab");
    TEST_ASSERT("Escape HTML: special chars", escape_html("<div class='foo' id=\"bar\">&") == "&lt;div class=&#39;foo&#39; id=&quot;bar&quot;&gt;&amp;");

    // -------------------------------------------------------------------------
    // CATEGORY 3: Descriptive Statistics Engine (Tests 31 - 45)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 3: Descriptive Statistics Engine ---\n";

    std::vector<double> empty_vec;
    TEST_ASSERT("Stats mean: empty", Statistics::mean(empty_vec) == 0.0);
    TEST_ASSERT("Stats median: empty", Statistics::median(empty_vec) == 0.0);

    std::vector<double> single_val = { 42.0 };
    TEST_ASSERT("Stats mean: single value", Statistics::mean(single_val) == 42.0);
    TEST_ASSERT("Stats median: single value", Statistics::median(single_val) == 42.0);

    std::vector<double> dataset = { 10.0, 20.0, 30.0, 40.0, 50.0 };
    TEST_ASSERT_NEAR("Stats mean: odd dataset", Statistics::mean(dataset), 30.0, 1e-6);
    TEST_ASSERT_NEAR("Stats median: odd dataset", Statistics::median(dataset), 30.0, 1e-6);
    TEST_ASSERT_NEAR("Stats variance: dataset", Statistics::variance(dataset), 250.0, 1e-6);
    TEST_ASSERT_NEAR("Stats std_dev: dataset", Statistics::std_dev(dataset), std::sqrt(250.0), 1e-6);

    std::vector<double> even_dataset = { 1.0, 2.0, 3.0, 4.0 };
    TEST_ASSERT_NEAR("Stats median: even dataset", Statistics::median(even_dataset), 2.5, 1e-6);

    TEST_ASSERT_NEAR("Stats percentile: p0", Statistics::percentile(dataset, 0.0), 10.0, 1e-6);
    TEST_ASSERT_NEAR("Stats percentile: p50", Statistics::percentile(dataset, 0.5), 30.0, 1e-6);
    TEST_ASSERT_NEAR("Stats percentile: p100", Statistics::percentile(dataset, 1.0), 50.0, 1e-6);

    auto summary = Statistics::summarize(dataset);
    TEST_ASSERT("Stats summarize: count", summary.count == 5.0);
    TEST_ASSERT("Stats summarize: min", summary.min_val == 10.0);
    TEST_ASSERT("Stats summarize: max", summary.max_val == 50.0);

    // -------------------------------------------------------------------------
    // CATEGORY 4: Advanced Statistical Filters & Robust Estimators (Tests 46 - 55)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 4: Advanced Statistical Filters & Robust Estimators ---\n";

    std::vector<double> sma_data = { 2.0, 4.0, 6.0, 8.0, 10.0 };
    auto sma = Statistics::simple_moving_average(sma_data, 3);
    TEST_ASSERT("SMA size matches input", sma.size() == 5);
    TEST_ASSERT_NEAR("SMA window 3 value at idx 2", sma[2], 4.0, 1e-6);
    TEST_ASSERT_NEAR("SMA window 3 value at idx 4", sma[4], 8.0, 1e-6);

    auto ema = Statistics::exponential_moving_average(sma_data, 0.5);
    TEST_ASSERT("EMA size matches input", ema.size() == 5);
    TEST_ASSERT_NEAR("EMA initial value", ema[0], 2.0, 1e-6);

    std::vector<double> outlier_data = { 10.0, 11.0, 10.5, 10.2, 100.0 };
    double mad = Statistics::median_absolute_deviation(outlier_data);
    TEST_ASSERT("MAD calculates positive deviation", mad > 0.0);

    std::vector<std::pair<double, uint64_t>> pts_with_outlier = {
        {1.0, 10}, {2.0, 20}, {3.0, 30}, {4.0, 40}, {5.0, 500}
    };
    auto filtered = Statistics::filter_outliers_mad(pts_with_outlier, 3.0);
    TEST_ASSERT("MAD filter removes extreme outlier", filtered.size() == 4);

    double theil_slope = Statistics::theil_sen_slope(pts_with_outlier);
    TEST_ASSERT_NEAR("Theil-Sen slope robust against outlier", theil_slope, 10.0, 1.0);

    // -------------------------------------------------------------------------
    // CATEGORY 5: Least-Squares Linear Regression (Tests 56 - 70)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 5: Least-Squares Linear Regression ---\n";

    std::vector<std::pair<double, uint64_t>> empty_pts;
    auto reg_empty = Regression::compute(empty_pts);
    TEST_ASSERT("Regression: empty points", reg_empty.sample_count == 0);

    std::vector<std::pair<double, uint64_t>> one_pt = { { 1.0, 100 } };
    auto reg_one = Regression::compute(one_pt);
    TEST_ASSERT("Regression: single point initial value", reg_one.initial_val == 100);
    TEST_ASSERT("Regression: single point slope zero", reg_one.slope == 0.0);

    // Perfect line: y = 200 * x + 50
    std::vector<std::pair<double, uint64_t>> perfect_line = {
        {1.0, 250}, {2.0, 450}, {3.0, 650}, {4.0, 850}, {5.0, 1050}
    };
    auto reg_perf = Regression::compute(perfect_line);
    TEST_ASSERT_NEAR("Regression perfect line: slope", reg_perf.slope, 200.0, 1e-6);
    TEST_ASSERT_NEAR("Regression perfect line: intercept", reg_perf.intercept, 50.0, 1e-6);
    TEST_ASSERT_NEAR("Regression perfect line: R²", reg_perf.r_squared, 1.0, 1e-6);
    TEST_ASSERT_NEAR("Regression perfect line: Pearson r", reg_perf.pearson_r, 1.0, 1e-6);
    TEST_ASSERT("Regression perfect line: delta", reg_perf.delta_val == 800);
    TEST_ASSERT("Regression perfect line: peak", reg_perf.peak_val == 1050);
    TEST_ASSERT("Regression perfect line: min", reg_perf.min_val == 250);

    // Flat constant line: y = 100
    std::vector<std::pair<double, uint64_t>> flat_line = {
        {1.0, 100}, {2.0, 100}, {3.0, 100}, {4.0, 100}
    };
    auto reg_flat = Regression::compute(flat_line);
    TEST_ASSERT_NEAR("Regression flat line: slope 0", reg_flat.slope, 0.0, 1e-6);
    TEST_ASSERT("Regression flat line: R² is 1.0", reg_flat.r_squared == 1.0);
    TEST_ASSERT("Regression flat line: delta is 0", reg_flat.delta_val == 0);

    // Noisy line with positive trend
    std::vector<std::pair<double, uint64_t>> noisy_line = {
        {1.0, 102}, {2.0, 198}, {3.0, 305}, {4.0, 395}, {5.0, 503}
    };
    auto reg_noisy = Regression::compute(noisy_line);
    TEST_ASSERT("Regression noisy line: positive slope", reg_noisy.slope > 95.0 && reg_noisy.slope < 105.0);
    TEST_ASSERT("Regression noisy line: high R²", reg_noisy.r_squared > 0.99);

    // Negative trend (memory deallocation)
    std::vector<std::pair<double, uint64_t>> decr_line = {
        {1.0, 500}, {2.0, 400}, {3.0, 300}, {4.0, 200}
    };
    auto reg_decr = Regression::compute(decr_line);
    TEST_ASSERT_NEAR("Regression decreasing line: negative slope", reg_decr.slope, -100.0, 1e-6);
    TEST_ASSERT("Regression decreasing line: negative delta", reg_decr.delta_val == -300);

    // -------------------------------------------------------------------------
    // CATEGORY 6: Exponential Curve Fitting (Tests 71 - 75)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 6: Exponential Curve Fitting ---\n";

    // Exponential points: y ~ 100 * exp(0.2 * x)
    std::vector<std::pair<double, uint64_t>> exp_pts = {
        {1.0, 122}, {2.0, 149}, {3.0, 182}, {4.0, 222}, {5.0, 271}
    };
    auto exp_fit = Regression::compute_exponential(exp_pts);
    TEST_ASSERT("Exponential fit: positive b exponent", exp_fit.b > 0.15 && exp_fit.b < 0.25);
    TEST_ASSERT("Exponential fit: high R²", exp_fit.r_squared > 0.95);
    TEST_ASSERT("Exponential fit: acceleration flagged", exp_fit.is_accelerating);

    std::vector<std::pair<double, uint64_t>> linear_pts = {
        {1.0, 10}, {2.0, 20}, {3.0, 30}, {4.0, 40}
    };
    auto exp_lin = Regression::compute_exponential(linear_pts);
    TEST_ASSERT("Linear points do not trigger accelerating exponential leak", !exp_lin.is_accelerating);

    // -------------------------------------------------------------------------
    // CATEGORY 7: Sparklines & ASCII Visualizations (Tests 76 - 85)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 7: Sparklines & ASCII Visualizations ---\n";

    std::vector<double> empty_spark;
    TEST_ASSERT("Sparkline: empty vector", Sparkline::render_unicode(empty_spark) == "");
    TEST_ASSERT("Sparkline ascii: empty vector", Sparkline::render_ascii(empty_spark) == "");

    std::vector<double> constant_spark = { 5.0, 5.0, 5.0 };
    std::string spark_const = Sparkline::render_unicode(constant_spark);
    TEST_ASSERT("Sparkline constant renders baseline blocks", !spark_const.empty());

    std::vector<double> ramp_spark = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 };
    std::string spark_ramp = Sparkline::render_unicode(ramp_spark);
    TEST_ASSERT("Sparkline ramp has output", !spark_ramp.empty());

    std::string ascii_ramp = Sparkline::render_ascii(ramp_spark);
    TEST_ASSERT("ASCII sparkline ramp has 8 characters", ascii_ramp.length() == 8);
    TEST_ASSERT("ASCII sparkline first is lowest char", ascii_ramp.front() == '_');
    TEST_ASSERT("ASCII sparkline last is highest char", ascii_ramp.back() == '%');

    auto chart = Sparkline::render_chart(ramp_spark, 20, 4, "Memory Trend");
    TEST_ASSERT("ASCII chart: correct height with label line", chart.size() == 5);
    TEST_ASSERT("ASCII chart: label contains title", chart.back().find("Memory Trend") != std::string::npos);

    // -------------------------------------------------------------------------
    // CATEGORY 8: ProcFs & Socket State Parsing (Tests 86 - 92)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 8: ProcFs & Socket State Parsing ---\n";

    TEST_ASSERT("TCP state 1 is ESTABLISHED", ProcFs::parse_tcp_state(1) == "ESTABLISHED");
    TEST_ASSERT("TCP state 8 is CLOSE_WAIT", ProcFs::parse_tcp_state(8) == "CLOSE_WAIT");
    TEST_ASSERT("TCP state 10 is LISTEN", ProcFs::parse_tcp_state(10) == "LISTEN");
    TEST_ASSERT("TCP state 6 is TIME_WAIT", ProcFs::parse_tcp_state(6) == "TIME_WAIT");
    TEST_ASSERT("TCP state 99 is UNKNOWN", ProcFs::parse_tcp_state(99) == "UNKNOWN");

    std::string formatted_ip = ProcFs::format_ip_port("0100007F", 8080);
    TEST_ASSERT("IPv4 hex translation 0100007F is 127.0.0.1:8080", formatted_ip == "127.0.0.1:8080");

    uint64_t sys_cpu = ProcFs::read_system_total_cpu_ticks();
    TEST_ASSERT("Read system total cpu ticks returns non-zero on Linux", sys_cpu > 0);

    // -------------------------------------------------------------------------
    // CATEGORY 9: Policy Rule Engine & Anomaly Detection (Tests 93 - 105)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 9: Policy Rule Engine & Anomaly Detection ---\n";

    Config cfg;
    cfg.warmup_sec = 2.0;
    cfg.mem_leak_threshold_kbs = 50.0;
    cfg.fd_leak_threshold_per_min = 2.0;
    cfg.min_r_squared = 0.80;

    std::vector<Sample> warmup_samples;
    Sample s1; s1.elapsed_sec = 0.5; s1.aggregate.mem.anon_bytes = 1000000;
    Sample s2; s2.elapsed_sec = 1.0; s2.aggregate.mem.anon_bytes = 2000000;
    warmup_samples.push_back(s1);
    warmup_samples.push_back(s2);

    auto verdict_warmup = Detector::evaluate(warmup_samples, cfg);
    TEST_ASSERT("Detector respects warmup grace period", verdict_warmup.severity == Severity::WARMUP);

    // Generate samples with steady leak
    std::vector<Sample> leaky_samples;
    for (int i = 0; i < 15; ++i) {
        Sample s;
        s.elapsed_sec = static_cast<double>(i) * 0.5;
        s.aggregate.pid = 1001;
        s.aggregate.comm = "leak_app";
        // Leak 200 KB per sample (400 KB/s)
        s.aggregate.mem.anon_bytes = 1024 * 1024 + static_cast<uint64_t>(i) * 200 * 1024;
        s.aggregate.mem.rss_bytes = s.aggregate.mem.anon_bytes + 2 * 1024 * 1024;
        s.aggregate.fds.total_fds = 10;
        s.aggregate.num_threads = 2;
        leaky_samples.push_back(s);
    }

    auto verdict_leak = Detector::evaluate(leaky_samples, cfg);
    TEST_ASSERT("Detector flags persistent memory leak", verdict_leak.is_leaking_memory);
    TEST_ASSERT("Detector severity is LEAK", verdict_leak.severity == Severity::LEAK);
    TEST_ASSERT("Detector memory reason populated", !verdict_leak.memory_reason.empty());
    TEST_ASSERT("Detector confidence score > 80%", verdict_leak.confidence_score >= 80.0);

    // Generate samples with stable memory
    std::vector<Sample> stable_samples;
    for (int i = 0; i < 15; ++i) {
        Sample s;
        s.elapsed_sec = static_cast<double>(i) * 0.5;
        s.aggregate.pid = 1002;
        s.aggregate.comm = "stable_app";
        s.aggregate.mem.anon_bytes = 5 * 1024 * 1024; // Constant 5 MB
        s.aggregate.mem.rss_bytes = 8 * 1024 * 1024;
        s.aggregate.fds.total_fds = 12;
        s.aggregate.num_threads = 4;
        stable_samples.push_back(s);
    }

    auto verdict_stable = Detector::evaluate(stable_samples, cfg);
    TEST_ASSERT("Detector does not flag stable memory", !verdict_stable.is_leaking_memory);
    TEST_ASSERT("Detector severity is OK for stable app", verdict_stable.severity == Severity::OK);

    // Generate samples with leaking FDs
    std::vector<Sample> fd_leaky_samples;
    for (int i = 0; i < 15; ++i) {
        Sample s;
        s.elapsed_sec = static_cast<double>(i) * 0.5;
        s.aggregate.pid = 1003;
        s.aggregate.comm = "fd_leak_app";
        s.aggregate.mem.anon_bytes = 2 * 1024 * 1024;
        s.aggregate.mem.rss_bytes = 3 * 1024 * 1024;
        s.aggregate.fds.total_fds = 10 + i * 2; // +2 FDs every 0.5s = +240 FDs/min
        s.aggregate.num_threads = 1;
        fd_leaky_samples.push_back(s);
    }

    auto verdict_fd = Detector::evaluate(fd_leaky_samples, cfg);
    TEST_ASSERT("Detector flags file descriptor leak", verdict_fd.is_leaking_fds);
    TEST_ASSERT("Detector severity is LEAK for FD leak", verdict_fd.severity == Severity::LEAK);

    // Hard ceiling assertion check
    cfg.max_anon_mb = 4.0; // 4 MB ceiling
    auto verdict_ceiling = Detector::evaluate(stable_samples, cfg); // Stable is 5 MB
    TEST_ASSERT("Detector catches hard limit breach", verdict_ceiling.exceeded_limits);
    TEST_ASSERT("Detector limit reason populated", !verdict_ceiling.limit_reason.empty());

    // -------------------------------------------------------------------------
    // CATEGORY 10: CLI Parser & Config Validation (Tests 106 - 115)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 10: CLI Parser & Config Validation ---\n";

    char* argv1[] = { const_cast<char*>("leakspot"), const_cast<char*>("-p"), const_cast<char*>("9999") };
    auto cfg1 = CliParser::parse(3, argv1);
    TEST_ASSERT("CLI parser: -p 9999", cfg1.target_pid == 9999);

    char* argv2[] = {
        const_cast<char*>("leakspot"),
        const_cast<char*>("-i"), const_cast<char*>("0.2"),
        const_cast<char*>("-w"), const_cast<char*>("4.5"),
        const_cast<char*>("-r"), const_cast<char*>("0.85"),
        const_cast<char*>("-m"), const_cast<char*>("120.0"),
        const_cast<char*>("-d"), const_cast<char*>("5.0"),
        const_cast<char*>("--fail-on-leak"),
        const_cast<char*>("--no-exp"),
        const_cast<char*>("--"),
        const_cast<char*>("ls"), const_cast<char*>("-la")
    };
    auto cfg2 = CliParser::parse(16, argv2);
    TEST_ASSERT_NEAR("CLI parser: interval", cfg2.sample_interval_sec, 0.2, 1e-6);
    TEST_ASSERT_NEAR("CLI parser: warmup", cfg2.warmup_sec, 4.5, 1e-6);
    TEST_ASSERT_NEAR("CLI parser: min R²", cfg2.min_r_squared, 0.85, 1e-6);
    TEST_ASSERT_NEAR("CLI parser: mem threshold", cfg2.mem_leak_threshold_kbs, 120.0, 1e-6);
    TEST_ASSERT_NEAR("CLI parser: fd threshold", cfg2.fd_leak_threshold_per_min, 5.0, 1e-6);
    TEST_ASSERT("CLI parser: fail on leak set", cfg2.fail_on_leak == true);
    TEST_ASSERT("CLI parser: no-exp flag honored", cfg2.enable_exponential_check == false);
    TEST_ASSERT("CLI parser: command size", cfg2.command.size() == 2);
    TEST_ASSERT("CLI parser: command arg 0", cfg2.command[0] == "ls");
    TEST_ASSERT("CLI parser: command arg 1", cfg2.command[1] == "-la");

    // -------------------------------------------------------------------------
    // CATEGORY 11: Memory Map (smaps) Inspector (Tests 113 - 125)
    // -------------------------------------------------------------------------
    std::cout << "\n--- Category 11: Memory Map (smaps) Inspector ---\n";

    std::string mock_smaps =
        "00400000-00452000 r-xp 00000000 08:02 173521 /usr/lib/libc.so\n"
        "Size:                328 kB\n"
        "Rss:                 200 kB\n"
        "Pss:                 100 kB\n"
        "Anonymous:             0 kB\n"
        "Shared_Clean:        200 kB\n"
        "Shared_Dirty:          0 kB\n"
        "Private_Clean:         0 kB\n"
        "Private_Dirty:         0 kB\n"
        "Swap:                  0 kB\n"
        "01200000-01400000 rw-p 00000000 00:00 0 [heap]\n"
        "Size:               2048 kB\n"
        "Rss:                1500 kB\n"
        "Pss:                1500 kB\n"
        "Anonymous:          1500 kB\n"
        "Shared_Clean:          0 kB\n"
        "Shared_Dirty:          0 kB\n"
        "Private_Clean:         0 kB\n"
        "Private_Dirty:      1500 kB\n"
        "Swap:                  0 kB\n"
        "7f8a12000000-7f8a12200000 rw-p 00000000 00:00 0\n"
        "Size:               2048 kB\n"
        "Rss:                2048 kB\n"
        "Pss:                2048 kB\n"
        "Anonymous:          2048 kB\n"
        "Shared_Clean:          0 kB\n"
        "Shared_Dirty:          0 kB\n"
        "Private_Clean:         0 kB\n"
        "Private_Dirty:      2048 kB\n"
        "Swap:                  0 kB\n";

    auto parsed_maps = SmapsInspector::parse_smaps_content(mock_smaps);
    TEST_ASSERT("Smaps parser: parses 3 mappings", parsed_maps.size() == 3);
    TEST_ASSERT("Smaps parser: map 0 path", parsed_maps[0].pathname == "/usr/lib/libc.so");
    TEST_ASSERT("Smaps parser: map 0 perms", parsed_maps[0].perms == "r-xp");
    TEST_ASSERT("Smaps parser: map 0 size", parsed_maps[0].size_bytes == 328 * 1024);
    TEST_ASSERT("Smaps parser: map 1 is heap", parsed_maps[1].pathname == "[heap]");
    TEST_ASSERT("Smaps parser: map 1 anon bytes", parsed_maps[1].anon_bytes == 1500 * 1024);
    TEST_ASSERT("Smaps parser: map 2 is anonymous", parsed_maps[2].pathname == "[anon]");

    // Diffing tests
    const auto& baseline_maps = parsed_maps;
    auto expanded_maps = parsed_maps;
    // Simulate heap growing by 5 MB
    expanded_maps[1].anon_bytes += 5 * 1024 * 1024;
    expanded_maps[1].rss_bytes += 5 * 1024 * 1024;

    // Simulate brand new 3 MB allocation
    MemoryMapping new_m;
    new_m.start_addr = 0x7f8a20000000;
    new_m.end_addr = 0x7f8a20300000;
    new_m.pathname = "[anon:alloc]";
    new_m.perms = "rw-p";
    new_m.anon_bytes = 3 * 1024 * 1024;
    new_m.rss_bytes = 3 * 1024 * 1024;
    expanded_maps.push_back(new_m);

    auto diffs = SmapsInspector::diff_mappings(baseline_maps, expanded_maps, 5);
    TEST_ASSERT("Smaps diff: detects 2 expanding mappings", diffs.size() == 2);
    TEST_ASSERT("Smaps diff: top is heap", diffs[0].pathname == "[heap]");
    TEST_ASSERT("Smaps diff: heap delta is 5 MB", diffs[0].delta_anon == 5 * 1024 * 1024);
    TEST_ASSERT("Smaps diff: second is new mapping", diffs[1].is_new_mapping == true);
    TEST_ASSERT("Smaps diff: new mapping delta is 3 MB", diffs[1].delta_anon == 3 * 1024 * 1024);

    std::string report = SmapsInspector::format_diff_report(diffs);
    TEST_ASSERT("Smaps report contains header", report.find("TOP EXPANDING VIRTUAL MEMORY REGIONS") != std::string::npos);

    // -------------------------------------------------------------------------
    // SUMMARY
    // -------------------------------------------------------------------------
    std::cout << "\n================================================================================\n";
    std::cout << std::format("  TEST RESULTS: {} / {} PASSED ({} FAILED)\n",
        test_framework::passed_tests, test_framework::total_tests, test_framework::failed_tests);
    std::cout << "================================================================================\n\n";

    return (test_framework::failed_tests == 0) ? 0 : 1;
}

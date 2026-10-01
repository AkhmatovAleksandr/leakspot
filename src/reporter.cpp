#include "reporter.hpp"
#include "sparkline.hpp"
#include <iostream>
#include <format>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace leakspot {

namespace color {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* RED     = "\033[31m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* GRAY    = "\033[90m";
    constexpr const char* BOLD_RED = "\033[1;31m";
    constexpr const char* BOLD_GREEN = "\033[1;32m";
    constexpr const char* BOLD_YELLOW = "\033[1;33m";
}

Reporter::Reporter(const Config& config) : config_(config) {
    if (!config_.log_file.empty()) {
        log_stream_ = std::make_unique<std::ofstream>(config_.log_file);
    }

    if (config_.format == OutputFormat::CSV) {
        std::string header = "elapsed_sec,pid,comm,anon_bytes,rss_bytes,pss_bytes,swap_bytes,vm_size_bytes,total_fds,sockets,sockets_close_wait,pipes,files,threads,minflt,majflt,anon_slope_kbs,anon_r2,status";
        output_line(header);
    }
}

Reporter::~Reporter() {
    if (log_stream_ && log_stream_->is_open()) {
        log_stream_->close();
    }
}

void Reporter::output_line(const std::string& line) {
    std::cout << line << "\n";
    if (log_stream_ && log_stream_->is_open()) {
        *log_stream_ << line << "\n";
    }
}

void Reporter::print_header(const ProcessStats& initial) {
    if (config_.format == OutputFormat::JSON || config_.format == OutputFormat::CSV) {
        return;
    }

    std::string target_info = std::format("Target: {} (PID: {}) | Rate: {:.1f}s | Warmup: {:.1f}s",
        initial.comm, initial.pid, config_.sample_interval_sec, config_.warmup_sec);

    std::string banner = std::format(
        "{0}┌─────────────────────────────────────────────────────────────────────────────┐{1}\n"
        "{0}│{2}  leakspot v1.0.0 — Zero-Overhead Memory & Resource Leak Watcher           {0}│{1}\n"
        "{0}│{1}  {3:<73}  {0}│{1}\n"
        "{0}└─────────────────────────────────────────────────────────────────────────────┘{1}",
        color::CYAN, color::RESET, color::BOLD, target_info
    );
    output_line(banner);

    if (config_.format == OutputFormat::ANSI_TICKER) {
        std::string col_headers = std::format(
            "{0}{1}{2:<9} {3:<22} {4:<14} {5:<18} {6:<10} {7:<8} {8}{1}",
            color::BOLD, color::GRAY, "TIME", "ANONYMOUS (HEAP)", "RSS MEMORY", "OPEN FDs", "FAULTS", "THRDS", "STATUS"
        );
        output_line(col_headers);
        output_line(std::format("{0}--------------------------------------------------------------------------------{1}", color::GRAY, color::RESET));
    }
}

std::string Reporter::format_ansi_row(const Sample& sample, const LeakVerdict& verdict, const std::vector<Sample>& all_samples) {
    std::string time_str = format_duration(sample.elapsed_sec);

    // Anon Memory
    std::string anon_str = format_bytes(sample.aggregate.mem.anon_bytes);
    std::string anon_col;
    if (verdict.anon_regression.sample_count >= 2) {
        double kbs = verdict.anon_regression.slope / 1024.0;
        anon_col = std::format("{} ({:+.1f}K/s)", anon_str, kbs);
    } else {
        anon_col = anon_str;
    }

    // RSS Memory
    std::string rss_str = format_bytes(sample.aggregate.mem.rss_bytes);

    // FDs
    std::string fd_str = std::format("{} (s:{},p:{},f:{})",
        sample.aggregate.fds.total_fds,
        sample.aggregate.fds.sockets,
        sample.aggregate.fds.pipes,
        sample.aggregate.fds.files
    );

    // Faults
    std::string flt_str = std::format("{}/{}",
        sample.aggregate.mem.minor_faults,
        sample.aggregate.mem.major_faults
    );

    // Threads
    std::string th_str = std::to_string(sample.aggregate.num_threads);

    // Status
    std::string status_badge;
    switch (verdict.severity) {
        case Severity::WARMUP:
            status_badge = std::format("{}{}[WARMUP]{}", color::YELLOW, color::BOLD, color::RESET);
            break;
        case Severity::OK:
            status_badge = std::format("{}{}[OK]{}", color::GREEN, color::BOLD, color::RESET);
            break;
        case Severity::SUSPICIOUS:
            status_badge = std::format("{}{}[SUSPECT]{}", color::YELLOW, color::BOLD, color::RESET);
            break;
        case Severity::LEAK:
            status_badge = std::format("{}{}[LEAK DETECTED]{}", color::BOLD_RED, color::BOLD, color::RESET);
            break;
    }

    if (verdict.anon_regression.sample_count >= 3 && verdict.severity != Severity::WARMUP) {
        status_badge += std::format(" (R²={:.2f})", verdict.anon_regression.r_squared);
    }

    // Mini sparkline for the row (last 8 points)
    std::vector<double> recent_anon;
    size_t start_idx = (all_samples.size() > 8) ? all_samples.size() - 8 : 0;
    for (size_t i = start_idx; i < all_samples.size(); ++i) {
        recent_anon.push_back(static_cast<double>(all_samples[i].aggregate.mem.anon_bytes));
    }
    std::string spark = Sparkline::render_unicode(recent_anon, 8);

    return std::format(
        "[{0}] {1:<22} {2:<14} {3:<18} {4:<10} {5:<8} {6} {7}",
        time_str, anon_col, rss_str, fd_str, flt_str, th_str, status_badge, spark
    );
}

std::string Reporter::format_json_sample(const Sample& sample, const LeakVerdict& verdict) {
    std::string status_str = "OK";
    if (verdict.severity == Severity::WARMUP) status_str = "WARMUP";
    else if (verdict.severity == Severity::SUSPICIOUS) status_str = "SUSPICIOUS";
    else if (verdict.severity == Severity::LEAK) status_str = "LEAK";

    return std::format(
        "{{\"type\":\"sample\",\"elapsed_sec\":{:.2f},\"pid\":{},\"comm\":\"{}\","
        "\"anon_bytes\":{},\"rss_bytes\":{},\"pss_bytes\":{},\"swap_bytes\":{},\"vm_size_bytes\":{},"
        "\"total_fds\":{},\"sockets\":{},\"sockets_close_wait\":{},\"pipes\":{},\"files\":{},\"anon_inodes\":{},"
        "\"threads\":{},\"minflt\":{},\"majflt\":{},\"anon_slope_kbs\":{:.2f},\"anon_r2\":{:.3f},"
        "\"fd_slope_per_min\":{:.2f},\"fd_r2\":{:.3f},\"status\":\"{}\"}}",
        sample.elapsed_sec, sample.aggregate.pid, escape_json(sample.aggregate.comm),
        sample.aggregate.mem.anon_bytes, sample.aggregate.mem.rss_bytes,
        sample.aggregate.mem.pss_bytes, sample.aggregate.mem.swap_bytes,
        sample.aggregate.mem.vm_size_bytes,
        sample.aggregate.fds.total_fds, sample.aggregate.fds.sockets,
        sample.aggregate.fds.sockets_close_wait,
        sample.aggregate.fds.pipes, sample.aggregate.fds.files, sample.aggregate.fds.anon_inodes,
        sample.aggregate.num_threads,
        sample.aggregate.mem.minor_faults, sample.aggregate.mem.major_faults,
        verdict.anon_regression.slope / 1024.0, verdict.anon_regression.r_squared,
        verdict.fd_regression.slope * 60.0, verdict.fd_regression.r_squared,
        status_str
    );
}

std::string Reporter::format_csv_row(const Sample& sample, const LeakVerdict& verdict) {
    std::string status_str = "OK";
    if (verdict.severity == Severity::WARMUP) status_str = "WARMUP";
    else if (verdict.severity == Severity::SUSPICIOUS) status_str = "SUSPICIOUS";
    else if (verdict.severity == Severity::LEAK) status_str = "LEAK";

    return std::format(
        "{:.2f},{},\"{}\",{},{},{},{},{},{},{},{},{},{},{},{},{},{:.2f},{:.3f},{}",
        sample.elapsed_sec, sample.aggregate.pid, sample.aggregate.comm,
        sample.aggregate.mem.anon_bytes, sample.aggregate.mem.rss_bytes,
        sample.aggregate.mem.pss_bytes, sample.aggregate.mem.swap_bytes,
        sample.aggregate.mem.vm_size_bytes,
        sample.aggregate.fds.total_fds, sample.aggregate.fds.sockets,
        sample.aggregate.fds.sockets_close_wait,
        sample.aggregate.fds.pipes, sample.aggregate.fds.files,
        sample.aggregate.num_threads,
        sample.aggregate.mem.minor_faults, sample.aggregate.mem.major_faults,
        verdict.anon_regression.slope / 1024.0, verdict.anon_regression.r_squared,
        status_str
    );
}

void Reporter::report_sample(const Sample& sample, const LeakVerdict& verdict, const std::vector<Sample>& all_samples) {
    if (config_.format == OutputFormat::ANSI_TICKER) {
        output_line(format_ansi_row(sample, verdict, all_samples));
    } else if (config_.format == OutputFormat::JSON) {
        output_line(format_json_sample(sample, verdict));
    } else if (config_.format == OutputFormat::CSV) {
        output_line(format_csv_row(sample, verdict));
    }
}

void Reporter::report_alert(const LeakVerdict& verdict) {
    if (config_.format == OutputFormat::JSON || config_.format == OutputFormat::CSV) {
        return;
    }

    if (verdict.is_leaking_memory && !has_alerted_mem_) {
        has_alerted_mem_ = true;
        std::string alert = std::format(
            "\n{0}{1}>>> ALERT [MEMORY LEAK]: {2}{3}\n",
            color::BOLD_RED, color::BOLD, verdict.memory_reason, color::RESET
        );
        output_line(alert);
    }

    if (verdict.is_leaking_fds && !has_alerted_fd_) {
        has_alerted_fd_ = true;
        std::string alert = std::format(
            "\n{0}{1}>>> ALERT [FD RESOURCE LEAK]: {2}{3}\n",
            color::BOLD_RED, color::BOLD, verdict.fd_reason, color::RESET
        );
        output_line(alert);
    }

    if (verdict.is_leaking_threads && !has_alerted_thread_) {
        has_alerted_thread_ = true;
        std::string alert = std::format(
            "\n{0}{1}>>> ALERT [THREAD LEAK]: {2}{3}\n",
            color::BOLD_RED, color::BOLD, verdict.thread_reason, color::RESET
        );
        output_line(alert);
    }

    if (verdict.exceeded_limits && !has_alerted_limit_) {
        has_alerted_limit_ = true;
        std::string alert = std::format(
            "\n{0}{1}>>> ALERT [THRESHOLD EXCEEDED]: {2}{3}\n",
            color::BOLD_RED, color::BOLD, verdict.limit_reason, color::RESET
        );
        output_line(alert);
    }
}

void Reporter::generate_html_report(const std::vector<Sample>& samples, const LeakVerdict& final_verdict, const std::string& path) {
    std::ofstream html(path);
    if (!html.is_open()) return;

    std::string status_color = (final_verdict.severity == Severity::LEAK) ? "#ef4444" : "#22c55e";
    std::string status_text = (final_verdict.severity == Severity::LEAK) ? "LEAK DETECTED" : "PASSED (NO LEAKS)";

    html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    html << "<meta charset=\"UTF-8\">\n<title>leakspot Report - " << escape_html(samples.back().aggregate.comm) << "</title>\n";
    html << "<style>\n";
    html << "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 2rem; }\n";
    html << ".card { background: #1e293b; border-radius: 8px; padding: 1.5rem; margin-bottom: 1.5rem; border: 1px solid #334155; }\n";
    html << ".badge { display: inline-block; padding: 0.25rem 0.75rem; border-radius: 9999px; font-weight: bold; font-size: 0.875rem; }\n";
    html << "table { width: 100%; border-collapse: collapse; margin-top: 1rem; }\n";
    html << "th, td { text-align: left; padding: 0.75rem; border-bottom: 1px solid #334155; }\n";
    html << "th { color: #94a3b8; }\n";
    html << "</style>\n</head>\n<body>\n";

    html << "<h1>leakspot Audit Report</h1>\n";
    html << "<div class=\"card\">\n";
    html << "<h2>Process: " << escape_html(samples.back().aggregate.comm) << " (PID: " << samples.back().aggregate.pid << ")</h2>\n";
    html << "<span class=\"badge\" style=\"background:" << status_color << ";color:#fff;\">" << status_text << "</span>\n";
    html << "<p>Duration: " << format_duration(samples.back().elapsed_sec) << " | Samples: " << samples.size() << "</p>\n";
    html << "</div>\n";

    html << "<div class=\"card\">\n<h3>Telemetry Summary</h3>\n<table>\n";
    html << "<tr><th>Metric</th><th>Initial</th><th>Peak</th><th>Final</th><th>Growth Rate</th><th>R²</th></tr>\n";

    html << "<tr><td>Anonymous Memory</td><td>" << format_bytes(samples.front().aggregate.mem.anon_bytes) << "</td><td>"
         << format_bytes(final_verdict.anon_regression.peak_val) << "</td><td>"
         << format_bytes(samples.back().aggregate.mem.anon_bytes) << "</td><td>"
         << format_rate_kbs(final_verdict.anon_regression.slope) << "</td><td>"
         << std::format("{:.2f}", final_verdict.anon_regression.r_squared) << "</td></tr>\n";

    html << "<tr><td>Physical RSS</td><td>" << format_bytes(samples.front().aggregate.mem.rss_bytes) << "</td><td>"
         << format_bytes(final_verdict.rss_regression.peak_val) << "</td><td>"
         << format_bytes(samples.back().aggregate.mem.rss_bytes) << "</td><td>"
         << format_rate_kbs(final_verdict.rss_regression.slope) << "</td><td>"
         << std::format("{:.2f}", final_verdict.rss_regression.r_squared) << "</td></tr>\n";

    html << "<tr><td>Open FDs</td><td>" << samples.front().aggregate.fds.total_fds << "</td><td>"
         << final_verdict.fd_regression.peak_val << "</td><td>"
         << samples.back().aggregate.fds.total_fds << "</td><td>"
         << format_fd_rate(final_verdict.fd_regression.slope) << "</td><td>"
         << std::format("{:.2f}", final_verdict.fd_regression.r_squared) << "</td></tr>\n";

    html << "</table>\n</div>\n";

    if (!final_verdict.memory_reason.empty() || !final_verdict.fd_reason.empty()) {
        html << "<div class=\"card\" style=\"border-color: #ef4444;\">\n<h3 style=\"color:#ef4444;\">Diagnostic Findings</h3>\n<ul>\n";
        if (!final_verdict.memory_reason.empty()) html << "<li>" << escape_html(final_verdict.memory_reason) << "</li>\n";
        if (!final_verdict.fd_reason.empty()) html << "<li>" << escape_html(final_verdict.fd_reason) << "</li>\n";
        html << "</ul>\n</div>\n";
    }

    html << "</body>\n</html>\n";
    html.close();
}

void Reporter::print_summary(const std::vector<Sample>& samples, const LeakVerdict& final_verdict) {
    if (samples.empty()) {
        output_line("No telemetry samples recorded.");
        return;
    }

    if (!config_.html_report_path.empty()) {
        generate_html_report(samples, final_verdict, config_.html_report_path);
    }

    const auto& first = samples.front();
    const auto& last = samples.back();

    uint64_t peak_anon = 0;
    uint64_t peak_rss = 0;
    uint64_t peak_vmsize = 0;
    int peak_fds = 0;
    int peak_threads = 0;
    uint64_t peak_minflt = 0;
    uint64_t peak_majflt = 0;

    std::vector<double> all_anon_pts;
    std::vector<double> all_fd_pts;
    all_anon_pts.reserve(samples.size());
    all_fd_pts.reserve(samples.size());

    for (const auto& s : samples) {
        peak_anon = std::max(peak_anon, s.aggregate.mem.anon_bytes);
        peak_rss = std::max(peak_rss, s.aggregate.mem.rss_bytes);
        peak_vmsize = std::max(peak_vmsize, s.aggregate.mem.vm_size_bytes);
        peak_fds = std::max(peak_fds, s.aggregate.fds.total_fds);
        peak_threads = std::max(peak_threads, s.aggregate.num_threads);
        peak_minflt = std::max(peak_minflt, s.aggregate.mem.minor_faults);
        peak_majflt = std::max(peak_majflt, s.aggregate.mem.major_faults);

        all_anon_pts.push_back(static_cast<double>(s.aggregate.mem.anon_bytes) / 1024.0); // KB
        all_fd_pts.push_back(static_cast<double>(s.aggregate.fds.total_fds));
    }

    if (config_.format == OutputFormat::JSON) {
        std::string json_summary = std::format(
            "{{\"type\":\"summary\",\"elapsed_sec\":{:.2f},\"samples_count\":{},"
            "\"verdict\":\"{}\",\"is_leaking_memory\":{},\"is_leaking_fds\":{},\"is_leaking_threads\":{},"
            "\"exceeded_limits\":{},\"confidence_score\":{:.1f},\"initial_anon_bytes\":{},\"peak_anon_bytes\":{},"
            "\"final_anon_bytes\":{},\"anon_slope_kbs\":{:.2f},\"anon_r2\":{:.3f},"
            "\"initial_fds\":{},\"peak_fds\":{},\"final_fds\":{},"
            "\"fd_slope_per_min\":{:.2f},\"fd_r2\":{:.3f},\"close_wait_sockets\":{}}}",
            last.elapsed_sec, samples.size(),
            final_verdict.severity == Severity::LEAK ? "FAILED_LEAK" : "PASSED",
            final_verdict.is_leaking_memory ? "true" : "false",
            final_verdict.is_leaking_fds ? "true" : "false",
            final_verdict.is_leaking_threads ? "true" : "false",
            final_verdict.exceeded_limits ? "true" : "false",
            final_verdict.confidence_score,
            first.aggregate.mem.anon_bytes, peak_anon, last.aggregate.mem.anon_bytes,
            final_verdict.anon_regression.slope / 1024.0, final_verdict.anon_regression.r_squared,
            first.aggregate.fds.total_fds, peak_fds, last.aggregate.fds.total_fds,
            final_verdict.fd_regression.slope * 60.0, final_verdict.fd_regression.r_squared,
            last.aggregate.fds.sockets_close_wait
        );
        output_line(json_summary);
        return;
    }

    if (config_.format == OutputFormat::CSV) {
        return;
    }

    // Terminal Audit Report
    std::string verdict_badge;
    if (final_verdict.severity == Severity::LEAK) {
        verdict_badge = std::format("{}{}[FAILED - LEAK DETECTED]{}", color::BOLD_RED, color::BOLD, color::RESET);
    } else if (final_verdict.severity == Severity::SUSPICIOUS) {
        verdict_badge = std::format("{}{}[WARNING - SUSPICIOUS BEHAVIOR]{}", color::BOLD_YELLOW, color::BOLD, color::RESET);
    } else {
        verdict_badge = std::format("{}{}[PASSED - NO LEAKS DETECTED]{}", color::BOLD_GREEN, color::BOLD, color::RESET);
    }

    std::string summary = std::format(
        "\n{0}================================================================================{1}\n"
        "{2}                           LEAKSPOT POST-MORTEM AUDIT{1}\n"
        "{0}================================================================================{1}\n"
        "  Target Process:  {2}{3}{1} (PID: {4})\n"
        "  Total Duration:  {5} ({6} samples collected)\n"
        "  Final Verdict:   {7}\n\n"
        "  {8:<18} {9:<14} {10:<14} {11:<14} {12:<14} {13}\n"
        "  ------------------------------------------------------------------------------\n",
        color::CYAN, color::RESET, color::BOLD, last.aggregate.comm, last.aggregate.pid,
        format_duration(last.elapsed_sec), samples.size(), verdict_badge,
        "METRIC", "INITIAL", "PEAK", "FINAL", "DELTA", "REGRESSION TREND"
    );

    int64_t anon_delta = static_cast<int64_t>(last.aggregate.mem.anon_bytes) - static_cast<int64_t>(first.aggregate.mem.anon_bytes);
    int64_t rss_delta = static_cast<int64_t>(last.aggregate.mem.rss_bytes) - static_cast<int64_t>(first.aggregate.mem.rss_bytes);
    int64_t vm_delta = static_cast<int64_t>(last.aggregate.mem.vm_size_bytes) - static_cast<int64_t>(first.aggregate.mem.vm_size_bytes);
    int fd_delta = last.aggregate.fds.total_fds - first.aggregate.fds.total_fds;
    int th_delta = last.aggregate.num_threads - first.aggregate.num_threads;

    std::string anon_trend = std::format("{:+.1f} KB/s (R²={:.2f})", final_verdict.anon_regression.slope / 1024.0, final_verdict.anon_regression.r_squared);
    std::string rss_trend = std::format("{:+.1f} KB/s (R²={:.2f})", final_verdict.rss_regression.slope / 1024.0, final_verdict.rss_regression.r_squared);
    std::string fd_trend = std::format("{:+.1f} FDs/min (R²={:.2f})", final_verdict.fd_regression.slope * 60.0, final_verdict.fd_regression.r_squared);
    std::string th_trend = std::format("{:+.1f} th/min", final_verdict.thread_regression.slope * 60.0);

    summary += std::format(
        "  {0:<18} {1:<14} {2:<14} {3:<14} {4:<14} {5}\n"
        "  {6:<18} {7:<14} {8:<14} {9:<14} {10:<14} {11}\n"
        "  {12:<18} {13:<14} {14:<14} {15:<14} {16:<14}\n"
        "  {17:<18} {18:<14} {19:<14} {20:<14} {21:<14} {22}\n"
        "  {23:<18} {24:<14} {25:<14} {26:<14} {27:<14} {28}\n"
        "  {29:<18} {30:<14} {31:<14} {32:<14} {33:<14}\n"
        "  {34:<18} {35:<14} {36:<14} {37:<14} {38:<14}\n",
        "Anon Memory", format_bytes(first.aggregate.mem.anon_bytes), format_bytes(peak_anon), format_bytes(last.aggregate.mem.anon_bytes), std::format("{}{}", (anon_delta >= 0 ? "+" : ""), format_bytes(static_cast<uint64_t>(std::abs(anon_delta)))), anon_trend,
        "Resident (RSS)", format_bytes(first.aggregate.mem.rss_bytes), format_bytes(peak_rss), format_bytes(last.aggregate.mem.rss_bytes), std::format("{}{}", (rss_delta >= 0 ? "+" : ""), format_bytes(static_cast<uint64_t>(std::abs(rss_delta)))), rss_trend,
        "Virtual (VmSize)", format_bytes(first.aggregate.mem.vm_size_bytes), format_bytes(peak_vmsize), format_bytes(last.aggregate.mem.vm_size_bytes), std::format("{}{}", (vm_delta >= 0 ? "+" : ""), format_bytes(static_cast<uint64_t>(std::abs(vm_delta)))),
        "Open FDs", std::to_string(first.aggregate.fds.total_fds), std::to_string(peak_fds), std::to_string(last.aggregate.fds.total_fds), std::format("{:+d} handles", fd_delta), fd_trend,
        "Active Threads", std::to_string(first.aggregate.num_threads), std::to_string(peak_threads), std::to_string(last.aggregate.num_threads), std::format("{:+d}", th_delta), th_trend,
        "Minor PageFaults", std::to_string(first.aggregate.mem.minor_faults), std::to_string(peak_minflt), std::to_string(last.aggregate.mem.minor_faults), std::format("{:+d}", last.aggregate.mem.minor_faults - first.aggregate.mem.minor_faults),
        "Major PageFaults", std::to_string(first.aggregate.mem.major_faults), std::to_string(peak_majflt), std::to_string(last.aggregate.mem.major_faults), std::format("{:+d}", last.aggregate.mem.major_faults - first.aggregate.mem.major_faults)
    );

    // Sparkline summary row
    std::string anon_spark = Sparkline::render_unicode(all_anon_pts, 35);
    std::string fd_spark = Sparkline::render_unicode(all_fd_pts, 35);
    summary += std::format("\n  Anon History:   [{}]\n  FD History:     [{}]\n", anon_spark, fd_spark);

    if (last.aggregate.fds.sockets > 0) {
        summary += std::format(
            "  Socket Details: Total: {}, Established: {}, Listen: {}, Close-Wait: {}, Time-Wait: {}\n",
            last.aggregate.fds.sockets, last.aggregate.fds.sockets_established,
            last.aggregate.fds.sockets_listen, last.aggregate.fds.sockets_close_wait,
            last.aggregate.fds.sockets_time_wait
        );
    }

    if (final_verdict.severity == Severity::LEAK || final_verdict.severity == Severity::SUSPICIOUS) {
        summary += std::format("\n  {0}{1}DIAGNOSTIC FINDINGS:{2}\n", color::BOLD, color::YELLOW, color::RESET);
        if (final_verdict.is_leaking_memory) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.memory_reason, color::RESET);
        }
        if (final_verdict.is_leaking_fds) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.fd_reason, color::RESET);
        }
        if (final_verdict.is_leaking_threads) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.thread_reason, color::RESET);
        }
        if (final_verdict.exceeded_limits) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.limit_reason, color::RESET);
        }
    }

    summary += std::format("{0}================================================================================{1}", color::CYAN, color::RESET);
    output_line(summary);
}

} // namespace leakspot

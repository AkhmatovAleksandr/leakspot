#include "reporter.hpp"
#include <iostream>
#include <format>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace leakspot {

// ANSI Color Codes
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
        std::string header = "elapsed_sec,pid,comm,anon_bytes,rss_bytes,pss_bytes,swap_bytes,total_fds,sockets,pipes,files,minflt,majflt,anon_slope_kbs,anon_r2,status";
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
            "{0}{1}{2:<9} {3:<22} {4:<15} {5:<18} {6:<12} {7}{1}",
            color::BOLD, color::GRAY, "TIME", "ANONYMOUS (HEAP)", "RSS MEMORY", "OPEN FDs", "FAULTS", "STATUS"
        );
        output_line(col_headers);
        output_line(std::format("{0}--------------------------------------------------------------------------------{1}", color::GRAY, color::RESET));
    }
}

std::string format_time_min_sec(double seconds) {
    int total_sec = static_cast<int>(seconds);
    int mins = total_sec / 60;
    int secs = total_sec % 60;
    int ms = static_cast<int>((seconds - total_sec) * 10);
    return std::format("{:02d}:{:02d}.{}", mins, secs, ms);
}

std::string Reporter::format_ansi_row(const Sample& sample, const LeakVerdict& verdict) {
    std::string time_str = format_time_min_sec(sample.elapsed_sec);

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

    return std::format(
        "[{0}] {1:<22} {2:<15} {3:<18} {4:<12} {5}",
        time_str, anon_col, rss_str, fd_str, flt_str, status_badge
    );
}

std::string Reporter::format_json_sample(const Sample& sample, const LeakVerdict& verdict) {
    std::string status_str = "OK";
    if (verdict.severity == Severity::WARMUP) status_str = "WARMUP";
    else if (verdict.severity == Severity::SUSPICIOUS) status_str = "SUSPICIOUS";
    else if (verdict.severity == Severity::LEAK) status_str = "LEAK";

    return std::format(
        "{{\"type\":\"sample\",\"elapsed_sec\":{:.2f},\"pid\":{},\"comm\":\"{}\","
        "\"anon_bytes\":{},\"rss_bytes\":{},\"pss_bytes\":{},\"swap_bytes\":{},"
        "\"total_fds\":{},\"sockets\":{},\"pipes\":{},\"files\":{},\"anon_inodes\":{},"
        "\"minflt\":{},\"majflt\":{},\"anon_slope_kbs\":{:.2f},\"anon_r2\":{:.3f},"
        "\"fd_slope_per_min\":{:.2f},\"fd_r2\":{:.3f},\"status\":\"{}\"}}",
        sample.elapsed_sec, sample.aggregate.pid, sample.aggregate.comm,
        sample.aggregate.mem.anon_bytes, sample.aggregate.mem.rss_bytes,
        sample.aggregate.mem.pss_bytes, sample.aggregate.mem.swap_bytes,
        sample.aggregate.fds.total_fds, sample.aggregate.fds.sockets,
        sample.aggregate.fds.pipes, sample.aggregate.fds.files, sample.aggregate.fds.anon_inodes,
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
        "{:.2f},{},{},{},{},{},{},{},{},{},{},{},{},{:.2f},{:.3f},{}",
        sample.elapsed_sec, sample.aggregate.pid, sample.aggregate.comm,
        sample.aggregate.mem.anon_bytes, sample.aggregate.mem.rss_bytes,
        sample.aggregate.mem.pss_bytes, sample.aggregate.mem.swap_bytes,
        sample.aggregate.fds.total_fds, sample.aggregate.fds.sockets,
        sample.aggregate.fds.pipes, sample.aggregate.fds.files,
        sample.aggregate.mem.minor_faults, sample.aggregate.mem.major_faults,
        verdict.anon_regression.slope / 1024.0, verdict.anon_regression.r_squared,
        status_str
    );
}

void Reporter::report_sample(const Sample& sample, const LeakVerdict& verdict) {
    if (config_.format == OutputFormat::ANSI_TICKER) {
        output_line(format_ansi_row(sample, verdict));
    } else if (config_.format == OutputFormat::JSON) {
        output_line(format_json_sample(sample, verdict));
    } else if (config_.format == OutputFormat::CSV) {
        output_line(format_csv_row(sample, verdict));
    }
    // QUIET does not output rows
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

    if (verdict.exceeded_limits && !has_alerted_limit_) {
        has_alerted_limit_ = true;
        std::string alert = std::format(
            "\n{0}{1}>>> ALERT [THRESHOLD EXCEEDED]: {2}{3}\n",
            color::BOLD_RED, color::BOLD, verdict.limit_reason, color::RESET
        );
        output_line(alert);
    }
}

void Reporter::print_summary(const std::vector<Sample>& samples, const LeakVerdict& final_verdict) {
    if (samples.empty()) {
        output_line("No telemetry samples recorded.");
        return;
    }

    const auto& first = samples.front();
    const auto& last = samples.back();

    uint64_t peak_anon = 0;
    uint64_t peak_rss = 0;
    int peak_fds = 0;
    uint64_t peak_minflt = 0;
    uint64_t peak_majflt = 0;

    for (const auto& s : samples) {
        peak_anon = std::max(peak_anon, s.aggregate.mem.anon_bytes);
        peak_rss = std::max(peak_rss, s.aggregate.mem.rss_bytes);
        peak_fds = std::max(peak_fds, s.aggregate.fds.total_fds);
        peak_minflt = std::max(peak_minflt, s.aggregate.mem.minor_faults);
        peak_majflt = std::max(peak_majflt, s.aggregate.mem.major_faults);
    }

    if (config_.format == OutputFormat::JSON) {
        std::string json_summary = std::format(
            "{{\"type\":\"summary\",\"elapsed_sec\":{:.2f},\"samples_count\":{},"
            "\"verdict\":\"{}\",\"is_leaking_memory\":{},\"is_leaking_fds\":{},"
            "\"exceeded_limits\":{},\"initial_anon_bytes\":{},\"peak_anon_bytes\":{},"
            "\"final_anon_bytes\":{},\"anon_slope_kbs\":{:.2f},\"anon_r2\":{:.3f},"
            "\"initial_fds\":{},\"peak_fds\":{},\"final_fds\":{},"
            "\"fd_slope_per_min\":{:.2f},\"fd_r2\":{:.3f}}}",
            last.elapsed_sec, samples.size(),
            final_verdict.severity == Severity::LEAK ? "FAILED_LEAK" : "PASSED",
            final_verdict.is_leaking_memory ? "true" : "false",
            final_verdict.is_leaking_fds ? "true" : "false",
            final_verdict.exceeded_limits ? "true" : "false",
            first.aggregate.mem.anon_bytes, peak_anon, last.aggregate.mem.anon_bytes,
            final_verdict.anon_regression.slope / 1024.0, final_verdict.anon_regression.r_squared,
            first.aggregate.fds.total_fds, peak_fds, last.aggregate.fds.total_fds,
            final_verdict.fd_regression.slope * 60.0, final_verdict.fd_regression.r_squared
        );
        output_line(json_summary);
        return;
    }

    if (config_.format == OutputFormat::CSV) {
        return; // CSV stream already output
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
        format_time_min_sec(last.elapsed_sec), samples.size(), verdict_badge,
        "METRIC", "INITIAL", "PEAK", "FINAL", "DELTA", "REGRESSION TREND"
    );

    int64_t anon_delta = static_cast<int64_t>(last.aggregate.mem.anon_bytes) - static_cast<int64_t>(first.aggregate.mem.anon_bytes);
    int64_t rss_delta = static_cast<int64_t>(last.aggregate.mem.rss_bytes) - static_cast<int64_t>(first.aggregate.mem.rss_bytes);
    int fd_delta = last.aggregate.fds.total_fds - first.aggregate.fds.total_fds;

    std::string anon_trend = std::format("{:+.1f} KB/s (R²={:.2f})", final_verdict.anon_regression.slope / 1024.0, final_verdict.anon_regression.r_squared);
    std::string rss_trend = std::format("{:+.1f} KB/s (R²={:.2f})", final_verdict.rss_regression.slope / 1024.0, final_verdict.rss_regression.r_squared);
    std::string fd_trend = std::format("{:+.1f} FDs/min (R²={:.2f})", final_verdict.fd_regression.slope * 60.0, final_verdict.fd_regression.r_squared);

    summary += std::format(
        "  {0:<18} {1:<14} {2:<14} {3:<14} {4:<14} {5}\n"
        "  {6:<18} {7:<14} {8:<14} {9:<14} {10:<14} {11}\n"
        "  {12:<18} {13:<14} {14:<14} {15:<14} {16:<14} {17}\n"
        "  {18:<18} {19:<14} {20:<14} {21:<14} {22:<14}\n"
        "  {23:<18} {24:<14} {25:<14} {26:<14} {27:<14}\n",
        "Anon Memory", format_bytes(first.aggregate.mem.anon_bytes), format_bytes(peak_anon), format_bytes(last.aggregate.mem.anon_bytes), std::format("{}{}", (anon_delta >= 0 ? "+" : ""), format_bytes(static_cast<uint64_t>(std::abs(anon_delta)))), anon_trend,
        "Resident (RSS)", format_bytes(first.aggregate.mem.rss_bytes), format_bytes(peak_rss), format_bytes(last.aggregate.mem.rss_bytes), std::format("{}{}", (rss_delta >= 0 ? "+" : ""), format_bytes(static_cast<uint64_t>(std::abs(rss_delta)))), rss_trend,
        "Open FDs", std::to_string(first.aggregate.fds.total_fds), std::to_string(peak_fds), std::to_string(last.aggregate.fds.total_fds), std::format("{:+d} handles", fd_delta), fd_trend,
        "Minor PageFaults", std::to_string(first.aggregate.mem.minor_faults), std::to_string(peak_minflt), std::to_string(last.aggregate.mem.minor_faults), std::format("{:+d}", last.aggregate.mem.minor_faults - first.aggregate.mem.minor_faults),
        "Major PageFaults", std::to_string(first.aggregate.mem.major_faults), std::to_string(peak_majflt), std::to_string(last.aggregate.mem.major_faults), std::format("{:+d}", last.aggregate.mem.major_faults - first.aggregate.mem.major_faults)
    );

    if (final_verdict.severity == Severity::LEAK || final_verdict.severity == Severity::SUSPICIOUS) {
        summary += std::format("\n  {0}{1}DIAGNOSTIC FINDINGS:{2}\n", color::BOLD, color::YELLOW, color::RESET);
        if (final_verdict.is_leaking_memory) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.memory_reason, color::RESET);
        }
        if (final_verdict.is_leaking_fds) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.fd_reason, color::RESET);
        }
        if (final_verdict.exceeded_limits) {
            summary += std::format("  {0}[!] {1}{2}\n", color::RED, final_verdict.limit_reason, color::RESET);
        }
    }

    summary += std::format("{0}================================================================================{1}", color::CYAN, color::RESET);
    output_line(summary);
}

} // namespace leakspot

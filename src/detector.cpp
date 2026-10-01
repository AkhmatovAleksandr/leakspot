#include "detector.hpp"
#include "regression.hpp"
#include <format>

namespace leakspot {

LeakVerdict Detector::evaluate(const std::vector<Sample>& samples, const Config& config) {
    LeakVerdict verdict;

    if (samples.empty()) {
        verdict.severity = Severity::WARMUP;
        return verdict;
    }

    const auto& latest = samples.back();

    // Check if still in warm-up period
    if (latest.elapsed_sec < config.warmup_sec) {
        verdict.severity = Severity::WARMUP;
        return verdict;
    }

    // Compute regressions post-warmup
    verdict.anon_regression = Regression::compute_memory_trend(samples, config.warmup_sec, true);
    verdict.rss_regression = Regression::compute_rss_trend(samples, config.warmup_sec);
    verdict.fd_regression = Regression::compute_fd_trend(samples, config.warmup_sec);

    // Need at least 3 samples post-warmup for statistical validity
    if (verdict.anon_regression.sample_count < 3) {
        verdict.severity = Severity::WARMUP;
        return verdict;
    }

    // 1. Check Hard Limits (CI/CD assertions)
    double current_rss_mb = static_cast<double>(latest.aggregate.mem.rss_bytes) / (1024.0 * 1024.0);
    double current_anon_mb = static_cast<double>(latest.aggregate.mem.anon_bytes) / (1024.0 * 1024.0);
    int current_fds = latest.aggregate.fds.total_fds;

    if (config.max_rss_mb > 0.0 && current_rss_mb > config.max_rss_mb) {
        verdict.exceeded_limits = true;
        verdict.limit_reason = std::format("RSS ({:.1f} MB) exceeded limit ({:.1f} MB)", current_rss_mb, config.max_rss_mb);
    } else if (config.max_anon_mb > 0.0 && current_anon_mb > config.max_anon_mb) {
        verdict.exceeded_limits = true;
        verdict.limit_reason = std::format("Anon memory ({:.1f} MB) exceeded limit ({:.1f} MB)", current_anon_mb, config.max_anon_mb);
    } else if (config.max_fds > 0 && current_fds > config.max_fds) {
        verdict.exceeded_limits = true;
        verdict.limit_reason = std::format("Open FDs ({}) exceeded limit ({})", current_fds, config.max_fds);
    }

    // 2. Statistical Memory Leak Detection (Anonymous Memory)
    double anon_growth_kbs = verdict.anon_regression.slope / 1024.0;
    bool high_confidence_mem = verdict.anon_regression.r_squared >= config.min_r_squared;
    bool positive_mem_delta = verdict.anon_regression.delta_val > 0;

    if (anon_growth_kbs >= config.mem_leak_threshold_kbs && high_confidence_mem && positive_mem_delta) {
        verdict.is_leaking_memory = true;
        verdict.memory_reason = std::format(
            "Persistent memory leak: +{:.1f} KB/s with high linear correlation (R²={:.2f}, Δ={})",
            anon_growth_kbs, verdict.anon_regression.r_squared, format_bytes(static_cast<uint64_t>(verdict.anon_regression.delta_val))
        );
    }

    // 3. Statistical File Descriptor Leak Detection
    double fd_growth_rate_per_min = verdict.fd_regression.slope * 60.0;
    bool high_confidence_fd = verdict.fd_regression.r_squared >= config.min_r_squared;
    bool positive_fd_delta = verdict.fd_regression.delta_val > 0;

    if (fd_growth_rate_per_min >= config.fd_leak_threshold_per_min && high_confidence_fd && positive_fd_delta) {
        verdict.is_leaking_fds = true;
        verdict.fd_reason = std::format(
            "Persistent FD leak: +{:.1f} FDs/min (R²={:.2f}, Δ=+{} handles)",
            fd_growth_rate_per_min, verdict.fd_regression.r_squared, verdict.fd_regression.delta_val
        );
    }

    // Determine overall severity
    if (verdict.is_leaking_memory || verdict.is_leaking_fds || verdict.exceeded_limits) {
        verdict.severity = Severity::LEAK;
    } else if (anon_growth_kbs > (config.mem_leak_threshold_kbs * 0.5) || fd_growth_rate_per_min > (config.fd_leak_threshold_per_min * 0.5)) {
        verdict.severity = Severity::SUSPICIOUS;
    } else {
        verdict.severity = Severity::OK;
    }

    return verdict;
}

} // namespace leakspot

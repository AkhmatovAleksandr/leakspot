#include "types.hpp"
#include <format>
#include <iomanip>
#include <sstream>

namespace leakspot {

std::string format_bytes(uint64_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;

    auto d_bytes = static_cast<double>(bytes);

    if (d_bytes >= GB) {
        return std::format("{:.2f} GB", d_bytes / GB);
    } else if (d_bytes >= MB) {
        return std::format("{:.1f} MB", d_bytes / MB);
    } else if (d_bytes >= KB) {
        return std::format("{:.1f} KB", d_bytes / KB);
    } else {
        return std::format("{} B", bytes);
    }
}

std::string format_rate_kbs(double bytes_per_sec) {
    double kb_s = bytes_per_sec / 1024.0;
    if (std::abs(kb_s) >= 1024.0) {
        return std::format("{:+.2f} MB/s", kb_s / 1024.0);
    }
    return std::format("{:+.1f} KB/s", kb_s);
}

std::string format_fd_rate(double fds_per_sec) {
    double fds_per_min = fds_per_sec * 60.0;
    return std::format("{:+.1f} FDs/min", fds_per_min);
}

} // namespace leakspot

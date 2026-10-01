#include "types.hpp"
#include <format>
#include <cmath>
#include <sstream>

namespace leakspot {

std::string format_bytes(uint64_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    constexpr double TB = GB * 1024.0;

    auto d = static_cast<double>(bytes);

    if (d >= TB) {
        return std::format("{:.2f} TB", d / TB);
    } else if (d >= GB) {
        return std::format("{:.2f} GB", d / GB);
    } else if (d >= MB) {
        return std::format("{:.1f} MB", d / MB);
    } else if (d >= KB) {
        return std::format("{:.1f} KB", d / KB);
    } else {
        return std::format("{} B", bytes);
    }
}

std::string format_rate_kbs(double bytes_per_sec) {
    double kb_s = bytes_per_sec / 1024.0;
    if (std::abs(kb_s) >= 1024.0 * 1024.0) {
        return std::format("{:+.2f} GB/s", kb_s / (1024.0 * 1024.0));
    } else if (std::abs(kb_s) >= 1024.0) {
        return std::format("{:+.2f} MB/s", kb_s / 1024.0);
    }
    return std::format("{:+.1f} KB/s", kb_s);
}

std::string format_fd_rate(double fds_per_sec) {
    double fds_per_min = fds_per_sec * 60.0;
    return std::format("{:+.1f} FDs/min", fds_per_min);
}

std::string format_duration(double seconds) {
    int total_sec = static_cast<int>(seconds);
    int hours = total_sec / 3600;
    int mins = (total_sec % 3600) / 60;
    int secs = total_sec % 60;
    int tenths = static_cast<int>(std::round((seconds - static_cast<double>(total_sec)) * 10.0));
    if (tenths >= 10) {
        tenths = 0;
        ++secs;
    }

    if (hours > 0) {
        return std::format("{:02d}:{:02d}:{:02d}.{}", hours, mins, secs, tenths);
    }
    return std::format("{:02d}:{:02d}.{}", mins, secs, tenths);
}

std::string format_number(uint64_t num) {
    std::string s = std::to_string(num);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(static_cast<size_t>(n), ",");
        n -= 3;
    }
    return s;
}

std::string escape_json(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b";  break;
            case '\f': ss << "\\f";  break;
            case '\n': ss << "\\n";  break;
            case '\r': ss << "\\r";  break;
            case '\t': ss << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    ss << buf;
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

std::string escape_html(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '&':  ss << "&amp;"; break;
            case '<':  ss << "&lt;"; break;
            case '>':  ss << "&gt;"; break;
            case '"':  ss << "&quot;"; break;
            case '\'': ss << "&#39;"; break;
            default:   ss << c; break;
        }
    }
    return ss.str();
}

} // namespace leakspot

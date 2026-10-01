// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace leakspot {

class Sparkline {
public:
    static std::string render_unicode(const std::vector<double>& values, size_t max_width = 30);
    static std::string render_ascii(const std::vector<double>& values, size_t max_width = 30);
    static std::vector<std::string> render_chart(
        const std::vector<double>& values,
        size_t width = 40,
        size_t height = 5,
        const std::string& label = ""
    );
};

} // namespace leakspot

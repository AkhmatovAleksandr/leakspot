// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include "types.hpp"
#include "policy.hpp"
#include <vector>

namespace leakspot {

class Detector {
public:
    static LeakVerdict evaluate(const std::vector<Sample>& samples, const Config& config);
};

} // namespace leakspot

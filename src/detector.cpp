// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "detector.hpp"

namespace leakspot {

LeakVerdict Detector::evaluate(const std::vector<Sample>& samples, const Config& config) {
    auto rules = PolicyEngine::evaluate_all(samples, config);
    return PolicyEngine::compile_verdict(rules, samples, config);
}

} // namespace leakspot

#include "detector.hpp"

namespace leakspot {

LeakVerdict Detector::evaluate(const std::vector<Sample>& samples, const Config& config) {
    auto rules = PolicyEngine::evaluate_all(samples, config);
    return PolicyEngine::compile_verdict(rules, samples, config);
}

} // namespace leakspot

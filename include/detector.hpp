#pragma once

#include "types.hpp"
#include <vector>

namespace leakspot {

class Detector {
public:
    static LeakVerdict evaluate(const std::vector<Sample>& samples, const Config& config);
};

} // namespace leakspot

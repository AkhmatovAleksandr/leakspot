// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include "types.hpp"
#include <string>
#include <vector>

namespace leakspot {

class CliParser {
public:
    static Config parse(int argc, char* argv[]);
    static void print_help(const char* prog_name);
    static void print_version();
};

} // namespace leakspot

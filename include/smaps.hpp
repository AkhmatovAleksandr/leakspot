// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

namespace leakspot {

struct MemoryMapping {
    uintptr_t start_addr{0};
    uintptr_t end_addr{0};
    std::string perms;          // e.g., "rw-p", "r-xp"
    uint64_t offset{0};
    std::string dev;
    ino_t inode{0};
    std::string pathname;       // e.g., "[heap]", "[stack]", "/usr/lib/libc.so", "[anon]"

    uint64_t size_bytes{0};
    uint64_t rss_bytes{0};
    uint64_t pss_bytes{0};
    uint64_t anon_bytes{0};
    uint64_t shared_clean{0};
    uint64_t shared_dirty{0};
    uint64_t private_clean{0};
    uint64_t private_dirty{0};
    uint64_t swap_bytes{0};
};

struct MappingDiff {
    std::string pathname;
    std::string perms;
    uintptr_t start_addr{0};
    uintptr_t end_addr{0};
    uint64_t initial_anon{0};
    uint64_t current_anon{0};
    int64_t delta_anon{0};
    uint64_t initial_rss{0};
    uint64_t current_rss{0};
    int64_t delta_rss{0};
    bool is_new_mapping{false};
};

class SmapsInspector {
public:
    static std::vector<MemoryMapping> parse_smaps(pid_t pid);
    static std::vector<MemoryMapping> parse_smaps_content(const std::string& content);
    static std::vector<MappingDiff> diff_mappings(
        const std::vector<MemoryMapping>& baseline,
        const std::vector<MemoryMapping>& current,
        size_t top_n = 5
    );
    static std::string format_diff_report(const std::vector<MappingDiff>& diffs);
};

} // namespace leakspot

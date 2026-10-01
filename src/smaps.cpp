// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "smaps.hpp"
#include "types.hpp"
#include <cstring>
#include <fstream>
#include <sstream>
#include <format>
#include <unordered_map>
#include <algorithm>

namespace leakspot {

std::vector<MemoryMapping> SmapsInspector::parse_smaps_content(const std::string& content) {
    std::vector<MemoryMapping> mappings;
    std::istringstream stream(content);
    std::string line;

    MemoryMapping current;
    bool has_mapping = false;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        auto dash_pos = line.find('-');
        auto space_pos = line.find(' ');
        bool is_header = (dash_pos != std::string::npos && (space_pos == std::string::npos || dash_pos < space_pos));

        if (is_header) {
            if (has_mapping) {
                mappings.push_back(current);
            }
            current = MemoryMapping();
            has_mapping = true;

            char perms_buf[16] = {0};
            char dev_buf[32] = {0};
            char path_buf[1024] = {0};
            unsigned long start = 0, end = 0, offset = 0, inode = 0;

            int n = sscanf(line.c_str(), "%lx-%lx %15s %lx %31s %lu %1023[^\n]",
                           &start, &end, perms_buf, &offset, dev_buf, &inode, path_buf);

            current.start_addr = static_cast<uintptr_t>(start);
            current.end_addr = static_cast<uintptr_t>(end);
            current.perms = perms_buf;
            current.offset = offset;
            current.dev = dev_buf;
            current.inode = static_cast<ino_t>(inode);

            if (n >= 7 && path_buf[0] != '\0') {
                std::string p = path_buf;
                // Trim leading whitespace
                size_t first = p.find_first_not_of(" \t");
                if (first != std::string::npos) {
                    current.pathname = p.substr(first);
                } else {
                    current.pathname = p;
                }
            } else {
                current.pathname = "[anon]";
            }
        } else if (has_mapping) {
            char key[64] = {0};
            uint64_t val = 0;
            if (sscanf(line.c_str(), "%63[^:]: %lu", key, &val) >= 2) {
                uint64_t bytes = val * 1024;
                if (strcmp(key, "Size") == 0) current.size_bytes = bytes;
                else if (strcmp(key, "Rss") == 0) current.rss_bytes = bytes;
                else if (strcmp(key, "Pss") == 0) current.pss_bytes = bytes;
                else if (strcmp(key, "Anonymous") == 0) current.anon_bytes = bytes;
                else if (strcmp(key, "Shared_Clean") == 0) current.shared_clean = bytes;
                else if (strcmp(key, "Shared_Dirty") == 0) current.shared_dirty = bytes;
                else if (strcmp(key, "Private_Clean") == 0) current.private_clean = bytes;
                else if (strcmp(key, "Private_Dirty") == 0) current.private_dirty = bytes;
                else if (strcmp(key, "Swap") == 0) current.swap_bytes = bytes;
            }
        }
    }

    if (has_mapping) {
        mappings.push_back(current);
    }

    return mappings;
}

std::vector<MemoryMapping> SmapsInspector::parse_smaps(pid_t pid) {
    std::string path = std::format("/proc/{}/smaps", pid);
    std::ifstream file(path);
    if (!file.is_open()) return {};

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parse_smaps_content(content);
}

std::vector<MappingDiff> SmapsInspector::diff_mappings(
    const std::vector<MemoryMapping>& baseline,
    const std::vector<MemoryMapping>& current,
    size_t top_n
) {
    std::unordered_map<uintptr_t, MemoryMapping> base_map;
    for (const auto& m : baseline) {
        base_map[m.start_addr] = m;
    }

    std::vector<MappingDiff> diffs;

    for (const auto& cur : current) {
        auto it = base_map.find(cur.start_addr);
        if (it != base_map.end()) {
            const auto& base = it->second;
            int64_t d_anon = static_cast<int64_t>(cur.anon_bytes) - static_cast<int64_t>(base.anon_bytes);
            int64_t d_rss = static_cast<int64_t>(cur.rss_bytes) - static_cast<int64_t>(base.rss_bytes);

            if (d_anon > 0 || d_rss > 0) {
                MappingDiff md;
                md.pathname = cur.pathname;
                md.perms = cur.perms;
                md.start_addr = cur.start_addr;
                md.end_addr = cur.end_addr;
                md.initial_anon = base.anon_bytes;
                md.current_anon = cur.anon_bytes;
                md.delta_anon = d_anon;
                md.initial_rss = base.rss_bytes;
                md.current_rss = cur.rss_bytes;
                md.delta_rss = d_rss;
                md.is_new_mapping = false;
                diffs.push_back(md);
            }
        } else {
            // New mapping allocated during execution!
            if (cur.anon_bytes > 0 || cur.rss_bytes > 0) {
                MappingDiff md;
                md.pathname = cur.pathname;
                md.perms = cur.perms;
                md.start_addr = cur.start_addr;
                md.end_addr = cur.end_addr;
                md.initial_anon = 0;
                md.current_anon = cur.anon_bytes;
                md.delta_anon = static_cast<int64_t>(cur.anon_bytes);
                md.initial_rss = 0;
                md.current_rss = cur.rss_bytes;
                md.delta_rss = static_cast<int64_t>(cur.rss_bytes);
                md.is_new_mapping = true;
                diffs.push_back(md);
            }
        }
    }

    // Sort descending by delta_anon, then delta_rss
    std::sort(diffs.begin(), diffs.end(), [](const MappingDiff& a, const MappingDiff& b) {
        if (a.delta_anon != b.delta_anon) return a.delta_anon > b.delta_anon;
        return a.delta_rss > b.delta_rss;
    });

    if (top_n > 0 && diffs.size() > top_n) {
        diffs.resize(top_n);
    }

    return diffs;
}

std::string SmapsInspector::format_diff_report(const std::vector<MappingDiff>& diffs) {
    if (diffs.empty()) {
        return "  No individual memory mapping growth detected.\n";
    }

    std::string out = "  TOP EXPANDING VIRTUAL MEMORY REGIONS (SMAPS AUDIT):\n";
    out += std::format("  {:<18} {:<8} {:<24} {:<14} {:<14} {}\n",
        "REGION NAME", "PERMS", "ADDRESS RANGE", "INITIAL ANON", "FINAL ANON", "GROWTH DELTA");
    out += "  --------------------------------------------------------------------------------\n";

    for (const auto& d : diffs) {
        std::string addr_range = std::format("{:08x}-{:08x}", d.start_addr, d.end_addr);
        std::string type_tag = d.is_new_mapping ? " [NEW]" : "";
        std::string name = d.pathname + type_tag;

        out += std::format(
            "  {:<18} {:<8} {:<24} {:<14} {:<14} +{}\n",
            name.substr(0, 18), d.perms, addr_range,
            format_bytes(d.initial_anon),
            format_bytes(d.current_anon),
            format_bytes(static_cast<uint64_t>(d.delta_anon))
        );
    }

    return out;
}

} // namespace leakspot

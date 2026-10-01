// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>

int main() {
    std::cout << "[leaky_memory] Starting workload. Leaking ~500 KB every 100ms...\n";
    std::vector<char*> leaked_blocks;

    for (int i = 0; i < 70; ++i) {
        constexpr size_t BLOCK_SIZE = 500 * 1024; // 500 KB
        char* buf = new char[BLOCK_SIZE];
        // Dirty the pages so kernel assigns Anonymous RSS pages
        std::memset(buf, 0xAA, BLOCK_SIZE);
        leaked_blocks.push_back(buf);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[leaky_memory] Workload complete. Exiting.\n";
    return 0;
}

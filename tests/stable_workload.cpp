#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>

int main() {
    std::cout << "[stable_workload] Warm-up: Initializing memory cache...\n";
    constexpr size_t CACHE_SIZE = 10 * 1024 * 1024; // 10 MB initial cache
    std::vector<char> cache(CACHE_SIZE, 0x42);

    // Warm-up phase (2 seconds)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "[stable_workload] Running steady-state workload (allocating & immediately freeing)...\n";
    for (int i = 0; i < 40; ++i) {
        // Allocate temporary buffer and immediately free it
        constexpr size_t TEMP_SIZE = 2 * 1024 * 1024;
        char* temp = new char[TEMP_SIZE];
        std::memset(temp, 0x11, TEMP_SIZE);
        delete[] temp;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[stable_workload] Steady-state complete without leaks. Exiting.\n";
    return 0;
}

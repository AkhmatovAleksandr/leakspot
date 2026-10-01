#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>

int main() {
    std::cout << "[leaky_fds] Starting workload. Leaking socket pairs every 150ms...\n";
    std::vector<int> leaked_fds;

    for (int i = 0; i < 45; ++i) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            leaked_fds.push_back(sv[0]);
            leaked_fds.push_back(sv[1]);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    std::cout << "[leaky_fds] Workload complete. Exiting.\n";
    return 0;
}

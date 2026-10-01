#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/types.h>

namespace leakspot {

class ProcFs {
public:
    static std::string read_cmdline(pid_t pid);
    static std::vector<ThreadStats> read_threads(pid_t pid);
    static std::unordered_map<ino_t, SocketDetails> parse_system_sockets();
    static uint64_t read_system_total_cpu_ticks();
    static std::string parse_tcp_state(int state_code);
    static std::string format_ip_port(const std::string& hex_addr, int port);
};

} // namespace leakspot

#pragma once

#include "types.hpp"
#include "procfs.hpp"
#include <optional>
#include <vector>
#include <unordered_map>

namespace leakspot {

class Sampler {
public:
    static bool is_alive(pid_t pid);
    static std::optional<ProcessStats> sample_process(pid_t pid, bool resolve_sockets = true);
    static std::vector<pid_t> get_child_pids(pid_t parent_pid);
    static ProcessStats aggregate_stats(const ProcessStats& parent, const std::vector<ProcessStats>& children);

private:
    static bool parse_smaps_rollup(pid_t pid, MemoryStats& mem);
    static bool parse_status(pid_t pid, ProcessStats& stats);
    static bool parse_stat(pid_t pid, ProcessStats& stats);
    static bool parse_fds(pid_t pid, FdStats& fds, const std::unordered_map<ino_t, SocketDetails>& sockets);
};

} // namespace leakspot

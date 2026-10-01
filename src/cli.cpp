// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Akhmatov Aleksandr Tarasovich

#include "cli.hpp"
#include <iostream>
#include <format>
#include <cstdlib>
#include <stdexcept>

namespace leakspot {

void CliParser::print_version() {
    std::cout << "leakspot v1.0.0 (x86_64-linux, C++23)\n";
}

void CliParser::print_help(const char* prog_name) {
    std::cout << std::format(
        "Usage: {0} [options] -p <PID>\n"
        "       {0} [options] -- <command> [args...]\n\n"
        "Zero-overhead process memory & resource leak watcher for Linux.\n\n"
        "Target Selection:\n"
        "  -p, --pid <PID>              Attach to existing running process\n"
        "  -- <command> [args...]       Spawn and supervise command until completion\n"
        "  --cwd <path>                 Working directory for spawned command\n"
        "  --env <KEY=VAL>              Inject environment variable into spawned process\n\n"
        "Sampling & Detection:\n"
        "  -i, --interval <sec>         Sampling interval in seconds (default: 0.5)\n"
        "  -w, --warmup <sec>           Warm-up period to ignore before leak detection (default: 3.0)\n"
        "  -t, --timeout <sec>          Maximum supervision time before terminating (default: 0=unlimited)\n"
        "  -r, --min-r2 <float>         Minimum R² fit confidence [0.0 - 1.0] (default: 0.80)\n"
        "  -m, --mem-threshold <KB/s>   Sustained memory growth rate for leak alert (default: 50.0)\n"
        "  -d, --fd-threshold <rate>    Sustained FD growth rate per minute for leak alert (default: 2.0)\n"
        "  -f, --follow-children        Aggregate metrics from spawned child processes\n"
        "  --no-exp                     Disable non-linear exponential acceleration detection\n"
        "  --no-sockets                 Disable detailed TCP/UDP socket resolution\n\n"
        "Hard Resource Limits (CI/CD Assertions):\n"
        "  --max-rss <MB>               Fail if RSS exceeds limit in MB\n"
        "  --max-anon <MB>              Fail if Anonymous memory exceeds limit in MB\n"
        "  --max-fds <count>            Fail if open file descriptors exceed count\n"
        "  --max-threads <count>        Fail if active threads exceed count\n"
        "  --fail-on-leak               Exit with code 1 if leak or limit violation is detected\n\n"
        "Output Formats:\n"
        "  --json                       Stream NDJSON events to stdout\n"
        "  --csv                        Stream CSV time-series rows to stdout\n"
        "  --html <path>                Generate standalone HTML report with charts\n"
        "  -q, --quiet                  Quiet mode: print only alerts and final verdict\n"
        "  -o, --output <file>          Write report / stream to file in addition to stdout\n\n"
        "General:\n"
        "  -v, --version                Display version information\n"
        "  -h, --help                   Display this help message\n\n"
        "Examples:\n"
        "  {0} --fail-on-leak -- ./my_server --port 8080\n"
        "  {0} -p 12345 -i 1.0 -w 5.0 --json\n"
        "  {0} --fail-on-leak --max-anon 256 --html report.html -- ./test_suite\n",
        prog_name
    );
}

Config CliParser::parse(int argc, char* argv[]) {
    Config config;

    int i = 1;
    while (i < argc) {
        std::string arg = argv[i];

        if (arg == "--") {
            for (int j = i + 1; j < argc; ++j) {
                config.command.emplace_back(argv[j]);
            }
            break;
        } else if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            std::exit(0);
        } else if (arg == "-p" || arg == "--pid") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -p/--pid requires a PID argument");
            }
            config.target_pid = std::stoi(argv[++i]);
        } else if (arg == "-i" || arg == "--interval") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -i/--interval requires a seconds argument");
            }
            config.sample_interval_sec = std::stod(argv[++i]);
            if (config.sample_interval_sec <= 0.0) {
                throw std::runtime_error("Sample interval must be positive");
            }
        } else if (arg == "-w" || arg == "--warmup") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -w/--warmup requires a seconds argument");
            }
            config.warmup_sec = std::stod(argv[++i]);
        } else if (arg == "-t" || arg == "--timeout") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -t/--timeout requires a seconds argument");
            }
            config.timeout_sec = std::stod(argv[++i]);
        } else if (arg == "-r" || arg == "--min-r2") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -r/--min-r2 requires a value between 0.0 and 1.0");
            }
            config.min_r_squared = std::stod(argv[++i]);
        } else if (arg == "-m" || arg == "--mem-threshold") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -m/--mem-threshold requires a KB/s value");
            }
            config.mem_leak_threshold_kbs = std::stod(argv[++i]);
        } else if (arg == "-d" || arg == "--fd-threshold") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -d/--fd-threshold requires an FDs/min value");
            }
            config.fd_leak_threshold_per_min = std::stod(argv[++i]);
        } else if (arg == "--max-rss") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --max-rss requires an MB limit");
            }
            config.max_rss_mb = std::stod(argv[++i]);
        } else if (arg == "--max-anon") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --max-anon requires an MB limit");
            }
            config.max_anon_mb = std::stod(argv[++i]);
        } else if (arg == "--max-fds") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --max-fds requires an integer limit");
            }
            config.max_fds = std::stoi(argv[++i]);
        } else if (arg == "--max-threads") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --max-threads requires an integer limit");
            }
            config.max_threads = std::stoi(argv[++i]);
        } else if (arg == "--cwd") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --cwd requires a directory path");
            }
            config.working_dir = argv[++i];
        } else if (arg == "--env") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --env requires a KEY=VAL argument");
            }
            std::string env_str = argv[++i];
            auto eq = env_str.find('=');
            if (eq != std::string::npos) {
                config.env_vars[env_str.substr(0, eq)] = env_str.substr(eq + 1);
            }
        } else if (arg == "-f" || arg == "--follow-children") {
            config.follow_children = true;
        } else if (arg == "--fail-on-leak") {
            config.fail_on_leak = true;
        } else if (arg == "--no-exp") {
            config.enable_exponential_check = false;
        } else if (arg == "--no-sockets") {
            config.resolve_socket_endpoints = false;
        } else if (arg == "--inspect-maps") {
            config.inspect_maps = true;
        } else if (arg == "--json") {
            config.format = OutputFormat::JSON;
        } else if (arg == "--csv") {
            config.format = OutputFormat::CSV;
        } else if (arg == "--html") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option --html requires a file path");
            }
            config.html_report_path = argv[++i];
        } else if (arg == "-q" || arg == "--quiet") {
            config.format = OutputFormat::QUIET;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Option -o/--output requires a file path");
            }
            config.log_file = argv[++i];
        } else {
            throw std::runtime_error(std::format("Unknown option: {}", arg));
        }

        ++i;
    }

    if (config.target_pid <= 0 && config.command.empty()) {
        print_help(argv[0]);
        throw std::runtime_error("Please specify either a PID with -p <PID> or a command with -- <cmd...>");
    }

    if (config.target_pid > 0 && !config.command.empty()) {
        throw std::runtime_error("Cannot specify both -p <PID> and -- <command...>");
    }

    return config;
}

} // namespace leakspot

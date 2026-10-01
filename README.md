# leakspot 🔍

**Zero-overhead process memory & resource leak watcher for Linux systems.**

[![Language](https://img.shields.io/badge/language-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-112%20passing-brightgreen.svg)]()
[![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen.svg)]()

`leakspot` is a standalone, high-performance systems tool designed to answer one critical question for any running service, binary, or test suite:
> ***"Is this process steadily leaking heap memory, file descriptors, or threads over time?"***

Unlike Valgrind or AddressSanitizer, `leakspot` requires **zero recompilation, no debug symbols, and introduces 0% runtime overhead**. It continuously inspects Linux kernel telemetry (`smaps_rollup`, `/proc/[pid]/status`, `/proc/[pid]/fd`, `/proc/net/tcp`) and runs **real-time least-squares linear and exponential regression** to statistically identify memory leaks, socket leaks, and handle exhaustion with high confidence ($R^2$).

---

## Key Features

* **Zero External Dependencies**: Pure modern C++23 standard library + native Linux `/proc` interfaces.
* **Dual Supervision Modes**:
  * Attach to any existing process: `leakspot -p <PID>`
  * Supervise command execution: `leakspot [options] -- <command> [args...]`
  * Process tree aggregation (`-f`, `--follow-children`) to track forks and child workers.
* **Deep Linux Kernel Telemetry**:
  * **Anonymous Memory (`RssAnon`)**: Isolates true heap and dirty anonymous allocations from file caches.
  * **Resident (`RSS`), Proportional (`PSS`), and Virtual (`VmSize`) Memory**.
  * **TCP/UDP Socket Resolution**: Resolves inodes to `local:port -> remote:port` and detects unclosed sockets accumulated in `CLOSE_WAIT` state.
  * **File Descriptor Categorization**: Sockets, Pipes/FIFOs, Regular Files, `anon_inode` handles, and Device nodes.
  * **Active Thread Tracking**: Monitors thread count deltas and per-thread states.
  * **Page Fault Dynamics**: Tracks minor (RAM reclaims) and major (disk I/O) page faults.
* **Statistical Detection Engine**:
  * **Least-Squares Linear Regression**: Calculates real-time slope ($KB/s$, $FDs/min$) and Pearson correlation ($R^2$).
  * **Exponential Acceleration Detection**: Identifies super-linear runaway leaks ($y = a \cdot e^{bx}$).
  * **Robust Outlier Filtering**: Median Absolute Deviation (MAD) and Theil-Sen robust slope estimation.
  * **Warm-up Grace Period**: Configurable `--warmup <sec>` to eliminate false alarms during cache initialization.
* **Visualizations & Reporters**:
  * **Live ANSI Sparklines**: Real-time in-terminal Unicode trendlines (` ▂▃▄▅▆▇█`).
  * **Post-Mortem Audit Report**: Formatted summary table with initial, peak, final, and delta metrics.
  * **Standalone HTML Dashboard** (`--html report.html`): Interactive dark-theme dashboard.
  * **Continuous NDJSON Stream** (`--json`) and **CSV Time-Series** (`--csv`).
* **CI/CD Assertions**:
  * Ceilings: `--max-rss <MB>`, `--max-anon <MB>`, `--max-fds <N>`, `--max-threads <N>`, `--timeout <sec>`.
  * Return code `1` on leak: `--fail-on-leak`.
* **Automated Ninja Test Suite**:
  * **112 Unit Tests** executing automatically on every `ninja` build.

---

## Building & Automatic Testing

```bash
git clone https://github.com/runvoid/leakspot.git
cd leakspot
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
ninja -C build
```

> **Note:** Ninja automatically compiles and executes the entire 112-test suite (`run_all_tests`) at the end of every build!

---

## Usage & Examples

### 1. Supervise a Command in CI with Leak Assertions
```bash
./build/leakspot --fail-on-leak --warmup 2.0 -- ./my_server --port 8080
```

### 2. Attach to a Running Daemon / Background Service
```bash
./build/leakspot -p $(pgrep redis-server) -i 1.0 -w 5.0
```

### 3. Generate a Standalone HTML Report
```bash
./build/leakspot --html audit_report.html -- ./data_processor
```

### 4. Stream Structured NDJSON to `jq`
```bash
./build/leakspot --json -- ./worker_daemon | jq 'select(.status == "LEAK")'
```

### 5. Set Hard Resource Ceilings & Execution Timeout
```bash
./build/leakspot --fail-on-leak --max-anon 256 --max-fds 100 --timeout 60.0 -- ./test_job
```

---

## Post-Mortem Audit Example

```
================================================================================
                           LEAKSPOT POST-MORTEM AUDIT
================================================================================
  Target Process:  worker (PID: 182377)
  Total Duration:  00:06.8 (18 samples collected)
  Final Verdict:   [FAILED - LEAK DETECTED]

  METRIC             INITIAL        PEAK           FINAL          DELTA          REGRESSION TREND
  ------------------------------------------------------------------------------
  Anon Memory        180.0 KB       34.2 MB        34.2 MB        +34.0 MB       +5086.2 KB/s (R²=1.00)
  Resident (RSS)     2.1 MB         37.8 MB        37.8 MB        +35.7 MB       +5086.2 KB/s (R²=1.00)
  Virtual (VmSize)   7.0 MB         41.2 MB        41.2 MB        +34.2 MB      
  Open FDs           8              8              8              +0 handles     +0.0 FDs/min (R²=1.00)
  Active Threads     1              1              1              +0             +0.0 th/min
  Minor PageFaults   116            8852           8852           +8736         
  Major PageFaults   0              0              0              +0            

  Anon History:   [   ▂▂▃▃▄▄▅▅▆▆▇▇███]
  FD History:     [                  ]
  Socket Details: Total: 2, Established: 0, Listen: 0, Close-Wait: 0, Time-Wait: 0

  DIAGNOSTIC FINDINGS:
  [!] Persistent memory leak: growing at +5086.2 KB/s (R²=1.00, Δ=26.1 MB)
================================================================================
```

---

## Command Line Options

| Flag | Description | Default |
| :--- | :--- | :--- |
| `-p, --pid <PID>` | Attach to existing running process | None |
| `-- <cmd> [args...]` | Launch and supervise command | None |
| `--cwd <path>` | Working directory for spawned command | Current dir |
| `--env <KEY=VAL>` | Inject environment variable | None |
| `-i, --interval <sec>` | Sampling interval in seconds | `0.5` |
| `-w, --warmup <sec>` | Warm-up period to ignore before leak detection | `3.0` |
| `-t, --timeout <sec>` | Maximum supervision time before terminating | `0` (unlimited) |
| `-r, --min-r2 <float>` | Minimum $R^2$ linear fit confidence | `0.80` |
| `-m, --mem-threshold <KB/s>` | Sustained memory growth rate to flag leak | `50.0` |
| `-d, --fd-threshold <rate>` | Sustained FD growth rate per minute to flag leak | `2.0` |
| `-f, --follow-children` | Track and aggregate child processes/forks | `false` |
| `--max-rss <MB>` | Hard ceiling for RSS in MB | None |
| `--max-anon <MB>` | Hard ceiling for Anonymous memory in MB | None |
| `--max-fds <count>` | Hard ceiling for open file descriptors | None |
| `--max-threads <count>` | Hard ceiling for active threads | None |
| `--fail-on-leak` | Exit with status code `1` on leak detection | `false` |
| `--no-exp` | Disable non-linear exponential acceleration detection | `false` |
| `--no-sockets` | Disable detailed socket resolution | `false` |
| `--html <path>` | Generate standalone HTML dashboard | None |
| `--json` | Stream NDJSON events to stdout | `false` |
| `--csv` | Stream CSV time-series to stdout | `false` |
| `-q, --quiet` | Output only alerts and final verdict table | `false` |
| `-o, --output <file>` | Write duplicate stream to file | None |

---

## Architecture

```mermaid
flowchart TD
    A["Target Process (PID or Command)"] --> B["Kernel /proc Subsystem"]
    B -->|smaps_rollup & status| C["ProcFs & Sampler"]
    B -->|/proc/[pid]/fd & fdinfo| C
    B -->|/proc/net/tcp, tcp6, udp| C
    B -->|/proc/[pid]/task| C
    C --> D["Sample Time-Series Database"]
    D --> E["Statistical Engine (Linear + Exp + MAD)"]
    E --> F["Policy Rule Engine (7 Diagnostic Rules)"]
    F -->|Alerts & Verdict| G["Reporter Subsystem"]
    G --> H["ANSI Live Ticker + Unicode Sparklines"]
    G --> I["Post-Mortem Audit Table"]
    G --> J["HTML Interactive Dashboard"]
    G --> K["NDJSON / CSV Streams"]
```

---

## License

MIT License. Crafted with precision for high-performance systems observability.

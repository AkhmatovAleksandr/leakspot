# leakspot 🔍

**Zero-overhead process memory & resource leak watcher for Linux systems.**

[![Language](https://img.shields.io/badge/language-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen.svg)]()

`leakspot` is a standalone, lightweight systems tool designed to answer one crucial question for any running binary or test suite:
> ***"Is this process steadily leaking heap memory or file descriptors over time?"***

Unlike Valgrind or ASan, `leakspot` requires **zero recompilation, no code changes, no debug symbols, and introduces 0% runtime overhead**. It continuously inspects kernel telemetry (`smaps_rollup`, `stat`, and `fd` tables) and runs **real-time least-squares linear regression** to statistically identify memory leaks and file descriptor exhaustion with high confidence ($R^2$).

---

## Key Features

* **Zero External Dependencies**: Pure C++23 standard library + native Linux `/proc` interfaces.
* **Dual Supervision Modes**:
  * Attach to any running process: `leakspot -p <PID>`
  * Supervise command execution: `leakspot [options] -- <command> [args...]`
* **Full Resource Telemetry**:
  * **Anonymous Memory (`RssAnon`)**: Filters out shared memory and file cache to isolate true heap/dirty allocations.
  * **Resident Set Size (`RSS`) & Proportional Set Size (`PSS`)**
  * **File Descriptor Categorization**: Sockets (TCP/UDP), Pipes/FIFOs, Regular Files, and `anon_inode` handles.
  * **Page Fault Dynamics**: Tracks minor (RAM reclaims) and major (disk I/O) page faults.
* **Statistical Leak Detection Engine**:
  * Configurable **warm-up grace period** (`--warmup <sec>`) to eliminate false positives from startup caches or JVM/allocator pools.
  * Calculates real-time growth slopes ($KB/s$ memory, $FDs/min$ handles).
  * Measures **$R^2$ determination coefficient** (Pearson correlation) to distinguish transient cache spikes from true monotonic leaks.
* **CI/CD Assertions**:
  * Set hard resource ceilings: `--max-rss <MB>`, `--max-anon <MB>`, `--max-fds <N>`.
  * Return exit code `1` automatically on leak detection: `--fail-on-leak`.
* **Multi-Format Output**:
  * **Human-friendly ANSI terminal ticker** with live color badges and post-mortem audit tables.
  * **Continuous NDJSON stream** (`--json`) for log aggregators and CI bots.
  * **CSV time-series log** (`--csv`) for graphing in gnuplot or Python pandas.
  * **Quiet mode** (`--quiet`) for silent CI/CD pipelines until alerts trigger.

---

## Building from Source

### Prerequisites
* Clang 18+ or GCC 14+ with C++23 support
* CMake 3.25+ and Ninja
* Mold linker (optional, detected automatically)

```bash
git clone https://github.com/runvoid/leakspot.git
cd leakspot
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
ninja -C build
```

Binary is output to `build/leakspot`.

---

## Usage & Examples

### 1. Supervise a Command in CI with Leak Assertions
```bash
./build/leakspot --fail-on-leak --warmup 2.0 -- ./my_server --port 8080
```
If a memory leak (> 50 KB/s with $R^2 \ge 0.80$) or FD leak is detected, `leakspot` exits with code `1`.

### 2. Attach to a Running Daemon / Background Service
```bash
./build/leakspot -p $(pgrep nginx) -i 1.0 -w 5.0
```

### 3. Stream Structured NDJSON to `jq` or Logstash
```bash
./build/leakspot --json -i 0.5 -- ./worker_daemon | jq 'select(.status == "LEAK")'
```

### 4. Enforce Hard Resource Ceilings
```bash
./build/leakspot --fail-on-leak --max-anon 128 --max-fds 50 -- ./data_processor
```

---

## Sample Post-Mortem Audit Report

```
================================================================================
                           LEAKSPOT POST-MORTEM AUDIT
================================================================================
  Target Process:  worker (PID: 177962)
  Total Duration:  00:06.8 (18 samples collected)
  Final Verdict:   [FAILED - LEAK DETECTED]

  METRIC             INITIAL        PEAK           FINAL          DELTA          REGRESSION TREND
  ------------------------------------------------------------------------------
  Anon Memory        8.0 KB         33.7 MB        33.7 MB        +33.7 MB       +5035.5 KB/s (R²=1.00)
  Resident (RSS)     8.0 KB         37.3 MB        37.3 MB        +37.3 MB       +5035.5 KB/s (R²=1.00)
  Open FDs           8              8              8              +0 handles     +0.0 FDs/min (R²=1.00)
  Minor PageFaults   29             8728           8728           +8699         
  Major PageFaults   0              0              0              +0            

  DIAGNOSTIC FINDINGS:
  [!] Persistent memory leak: +5035.5 KB/s with high linear correlation (R²=1.00, Δ=25.6 MB)
================================================================================
```

---

## CLI Options

| Flag | Description | Default |
| :--- | :--- | :--- |
| `-p, --pid <PID>` | Attach to existing process | None |
| `-- <cmd> [args...]` | Launch and supervise command | None |
| `-i, --interval <sec>` | Sampling interval in seconds | `0.5` |
| `-w, --warmup <sec>` | Warm-up period to ignore before leak detection | `3.0` |
| `-r, --min-r2 <float>` | Minimum $R^2$ determination coefficient confidence | `0.80` |
| `-m, --mem-threshold <KB/s>` | Sustained memory slope to flag leak | `50.0` |
| `-d, --fd-threshold <rate>` | Sustained FD slope (per minute) to flag leak | `2.0` |
| `-f, --follow-children` | Track and aggregate child processes/forks | `false` |
| `--max-rss <MB>` | Hard ceiling for RSS in MB | None |
| `--max-anon <MB>` | Hard ceiling for Anonymous memory in MB | None |
| `--max-fds <N>` | Hard ceiling for total open file descriptors | None |
| `--fail-on-leak` | Exit with status code `1` on detection | `false` |
| `--json` | Stream newline-delimited JSON events | `false` |
| `--csv` | Stream CSV time-series | `false` |
| `-q, --quiet` | Output only alerts and final verdict table | `false` |
| `-o, --output <file>` | Write duplicate stream to file | None |

---

## Architecture Overview

```mermaid
flowchart TD
    A["Target Process (PID or Command)"] --> B["Kernel /proc Subsystem"]
    B -->|smaps_rollup / status| C["Sampler Engine"]
    B -->|stat (Page Faults, Ticks)| C
    B -->|/proc/[pid]/fd (readlink)| C
    C --> D["Sample History Time-Series"]
    D --> E["Statistical Regression Engine (Least Squares)"]
    E -->|Slope & R² Estimation| F["Detector & Threshold Evaluator"]
    F -->|Verdict & Alerts| G["Reporter Pipeline"]
    G --> H["ANSI Live Ticker / Summary Table"]
    G --> I["NDJSON Stream"]
    G --> J["CSV Time-Series"]
```

---

## License

MIT License. Designed and crafted with precision for high-performance systems observation.

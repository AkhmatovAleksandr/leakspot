# fish completion for leakspot
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Akhmatov Aleksandr Tarasovich <mironovaleks620@gmail.com>

complete -c leakspot -s p -l pid -d "Attach to existing PID" -x -a "(__fish_complete_pids)"
complete -c leakspot -s i -l interval -d "Sampling interval in seconds" -r
complete -c leakspot -s w -l warmup -d "Warmup grace period in seconds" -r
complete -c leakspot -s t -l timeout -d "Max supervision duration in seconds" -r
complete -c leakspot -s m -l mem-threshold -d "Memory leak threshold (bytes/sec)" -r
complete -c leakspot -s d -l fd-threshold -d "FD leak threshold (FDs/min)" -r
complete -c leakspot -s r -l min-r2 -d "Minimum R² linearity threshold" -r
complete -c leakspot -s f -l follow-children -d "Aggregate metrics across child processes"
complete -c leakspot -l fail-on-leak -d "Exit with code 1 if leak detected"
complete -c leakspot -l max-rss -d "Fail if RSS exceeds limit in MB" -r
complete -c leakspot -l max-anon -d "Fail if Anonymous exceeds limit in MB" -r
complete -c leakspot -l max-fds -d "Fail if open file descriptors exceed count" -r
complete -c leakspot -l max-threads -d "Fail if active thread count exceeds count" -r
complete -c leakspot -l no-exp -d "Disable exponential acceleration detection"
complete -c leakspot -l no-sockets -d "Skip resolving TCP/UDP socket states"
complete -c leakspot -l inspect-maps -d "Deep /proc/[pid]/smaps address diffing"
complete -c leakspot -l html -d "Generate HTML dashboard report" -r -F
complete -c leakspot -l json -d "Stream NDJSON telemetry"
complete -c leakspot -l csv -d "Stream CSV telemetry"
complete -c leakspot -s q -l quiet -d "Suppress live ticker; post-mortem only"
complete -c leakspot -l cwd -d "Set child working directory" -r -a "(__fish_complete_directories)"
complete -c leakspot -l env -d "Inject environment variable (KEY=VAL)" -r
complete -c leakspot -s h -l help -d "Display help message"
complete -c leakspot -s v -l version -d "Display version information"

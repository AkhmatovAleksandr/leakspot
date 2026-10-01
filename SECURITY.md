# Security Policy

## Supported Versions

We release patches and security fixes for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

The `leakspot` project takes security and process isolation seriously. Because `leakspot` inspects system telemetry via `/proc` and executes or supervises child processes, maintaining zero privilege escalation vectors and safe path resolution is critical.

If you discover a security vulnerability or potential privilege issue within `leakspot`, please do **NOT** open a public issue on GitHub.

Instead, please report security vulnerabilities directly to:
* **Email:** [mironovaleks620@gmail.com](mailto:mironovaleks620@gmail.com)
* **Subject line:** `[SECURITY] leakspot vulnerability report - <brief description>`

Please include:
1. Steps to reproduce the issue (including sample code or reproduction script).
2. The expected vs actual behavior.
3. Your operating system version, kernel version, and architecture (`uname -a`).
4. Any relevant logs or stack traces.

### Response Timeline
* **Initial response:** Within 48 hours acknowledging receipt of the report.
* **Assessment & Fix:** Security patches will be prioritized and verified across the test suite before a coordinated public disclosure.

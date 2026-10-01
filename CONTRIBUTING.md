# Contributing to leakspot

Thank you for your interest in contributing to `leakspot`!

## Development Setup

`leakspot` is written in modern C++23 with zero external library dependencies.

### Prerequisites
* Clang 18+ or GCC 14+
* CMake 3.25+
* Ninja
* Mold linker (optional, detected automatically)

### Building & Running Tests
Every time you build with Ninja, the test suite executes automatically:
```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
ninja -C build
```

## Adding New Tests
Unit tests live in `tests/unit_tests.cpp`. When adding new telemetry rules, statistical routines, or CLI options:
1. Add corresponding test cases to `tests/unit_tests.cpp`.
2. Ensure `ninja -C build` passes all tests with zero failures.

## Code Style
* Modern C++23 idiomatic code (`std::format`, `std::chrono`, `std::filesystem`).
* Clear variable naming, strict type conversion warnings enabled (`-Wconversion`, `-Wsign-conversion`).
* Zero external runtime library dependencies.

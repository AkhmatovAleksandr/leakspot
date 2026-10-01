## Summary of Changes

<!-- Brief summary of what changes are introduced and the motivation behind them. -->

## Type of Change

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update
- [ ] Performance improvement / Refactoring
- [ ] Packaging / AUR / CI update

## Verification & Testing

- [ ] Built cleanly with Clang++ and Ninja (`cmake -B build -G Ninja && ninja -C build`)
- [ ] All automated unit tests passed (`125/125 passed`)
- [ ] Zero new external library dependencies introduced (pure C++23 stdlib + Linux kernel APIs)
- [ ] Tested against live memory or socket workloads if telemetry parsing was modified
- [ ] SPDX headers present on any new `.hpp` or `.cpp` files (`// SPDX-License-Identifier: GPL-3.0-or-later`)

## Related Issues

<!-- Closes #123 -->

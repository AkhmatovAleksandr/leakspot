#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Akhmatov Aleksandr Tarasovich <mironovaleks620@gmail.com>
#
# Release Preparation Script for leakspot (GitHub & AUR)
# Usage: ./scripts/prepare-release.sh <version>
# Example: ./scripts/prepare-release.sh 1.0.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ $# -ne 1 ]; then
    echo "Usage: $0 <version> (e.g. 1.0.0)"
    exit 1
fi

VERSION="${1#v}" # strip leading 'v' if present
TARBALL="leakspot-${VERSION}.tar.gz"
AUR_DIR="${ROOT_DIR}/packaging/aur"
AUR_GIT_DIR="${ROOT_DIR}/packaging/aur-git"

echo "=== Preparing leakspot Release v${VERSION} ==="

cd "${ROOT_DIR}"

# 1. Run unit test suite to guarantee 100% pass before release
echo ">>> Running test suite..."
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
ninja -C build run_all_tests

# 2. Generate release archive from HEAD
echo ">>> Generating distribution tarball: ${TARBALL}..."
git archive --format=tar.gz --prefix="leakspot-${VERSION}/" -o "${TARBALL}" HEAD

# 3. Calculate SHA256 checksum
SHA256=$(sha256sum "${TARBALL}" | awk '{print $1}')
echo ">>> Computed SHA256: ${SHA256}"

# 4. Update packaging/aur/PKGBUILD with new version and checksum
echo ">>> Updating packaging/aur/PKGBUILD..."
sed -i "s/^pkgver=.*/pkgver=${VERSION}/" "${AUR_DIR}/PKGBUILD"
sed -i "s/^pkgrel=.*/pkgrel=1/" "${AUR_DIR}/PKGBUILD"
sed -i "s/^sha256sums=.*/sha256sums=('${SHA256}')/" "${AUR_DIR}/PKGBUILD"

# 5. Regenerate .SRCINFO
echo ">>> Regenerating packaging/aur/.SRCINFO..."
(cd "${AUR_DIR}" && makepkg --printsrcinfo > .SRCINFO)

# 6. Regenerate packaging/aur-git/.SRCINFO
echo ">>> Regenerating packaging/aur-git/.SRCINFO..."
(cd "${AUR_GIT_DIR}" && makepkg --printsrcinfo > .SRCINFO)

# 7. Clean up temporary local tarball
rm -f "${TARBALL}"

echo ""
echo "=== Release v${VERSION} Preparation Complete ==="
echo "Next steps when ready to publish:"
echo "  1. Review changes: git diff packaging/"
echo "  2. Commit release: git commit -am 'chore(release): bump version to v${VERSION}'"
echo "  3. Create git tag: git tag -a 'v${VERSION}' -m 'Release v${VERSION}'"
echo "  4. Push to GitHub: git push origin main --tags"
echo "  5. Submit to AUR:  cd packaging/aur && git push origin master"

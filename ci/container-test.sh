#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Full container test (used by docker/run-tests.sh):
#   - copy the repo to /work/src
#   - build the .deb and lintian
#   - build the module against the distro headers
#   - install the .deb and build/verify the DKMS module

set -euo pipefail

src=${1:-${CI_PROJECT_DIR:-/src}}
work=${WORKDIR:-/work}

rm -rf "$work/src"
mkdir -p "$work"
cp -a "$src" "$work/src"

cd "$work/src/linux-trigger5"
echo "== building Debian package"
make debpkg

deb=$(ls -1t "$work/src"/trigger5-dkms_*.deb | head -n1)
echo "== lintian"
lintian "$deb"

echo "== module build"
"$work/src/ci/build-module.sh" "$work/src"

echo "== install test"
"$work/src/ci/test-install.sh" "$deb"

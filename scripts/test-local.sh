#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Local test run:
#   1. compile trigger5.ko against every installed kernel (headers required)
#   2. build the Debian package
#   3. run lintian on it
#
# Use scripts/dkms-install-local.sh (as root) to install the module for all
# installed kernels.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)

echo "== kernel compile matrix"
"$root/scripts/test-local-kernels.sh"

echo
echo "== Debian package"
(cd "$root/linux-trigger5" && make debpkg)

deb=$(ls -1t "$root"/trigger5-dkms_*.deb | head -n1)
echo "built: $deb"

if command -v lintian >/dev/null 2>&1; then
	echo
	echo "== lintian"
	lintian "$deb"
fi

echo
echo "== cleanup"
(cd "$root/linux-trigger5" && make debclean)

echo
echo "Done. To install for every installed kernel:"
echo "  sudo $root/scripts/dkms-install-local.sh"

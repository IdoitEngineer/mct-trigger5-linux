#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Build trigger5.ko against a kernel build tree.
#
#   ci/build-module.sh [repo-root] [KDIR]
#
# KDIR defaults to the first /lib/modules/*/build found (distro headers inside
# a container, or the running kernel on a normal machine).

set -euo pipefail

repo=${1:-$(cd "$(dirname "$0")/.." && pwd)}
kdir=${2:-}

if [ -z "$kdir" ]; then
	kdir=$(ls -d /lib/modules/*/build 2>/dev/null | head -n1 || true)
fi
if [ -z "$kdir" ] || [ ! -e "$kdir/Makefile" ]; then
	echo "no kernel build tree found (install the distro kernel headers)" >&2
	exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/trigger5-ci.XXXXXX")
trap 'rm -rf "$work"' EXIT

cp "$repo/linux-trigger5/trigger5.c" "$repo/linux-trigger5/Makefile" "$work/"

extra=()
[ -e "$kdir/Module.symvers" ] || extra+=(KBUILD_MODPOST_WARN=1)

echo "building trigger5.ko against $kdir"
make -C "$work" KDIR="$kdir" "${extra[@]}"

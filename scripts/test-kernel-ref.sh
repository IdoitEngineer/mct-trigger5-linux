#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# One-off build against a reference from the linux-stable git tree, to pin the
# DRM API boundaries (e.g. v7.0, v7.1, v7.2). This is a developer check: it is
# not used by the Docker tests or the GitLab pipeline.
#
#   scripts/test-kernel-ref.sh v7.1
#
# Env:
#   LINUX_STABLE   default ~/Software/Linux/linux-stable
#   BUILD_ROOT     default ~/Software/Linux/builds

set -euo pipefail

ref=${1:-}
if [ -z "$ref" ]; then
	echo "usage: $0 <git-ref>   (e.g. v7.0, v7.1, v7.2, v7.3)" >&2
	exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/linux-trigger5"
stable=${LINUX_STABLE:-$HOME/Software/Linux/linux-stable}
build_root=${BUILD_ROOT:-$HOME/Software/Linux/builds}
kdir="$build_root/$ref"

if [ ! -d "$stable" ]; then
	echo "linux-stable tree not found: $stable" >&2
	exit 1
fi

if [ ! -d "$kdir" ]; then
	mkdir -p "$build_root"
	echo "creating worktree $kdir"
	git -C "$stable" worktree add --detach "$kdir" "$ref"
fi

if [ ! -e "$kdir/include/config/auto.conf" ]; then
	echo "preparing $ref"
	make -C "$kdir" defconfig
	make -C "$kdir" -j"$(nproc)" modules_prepare
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/trigger5-ref.XXXXXX")
trap 'rm -rf "$work"' EXIT

cp "$src/trigger5.c" "$src/Makefile" "$work/"

echo
echo "building trigger5.ko against $ref ($kdir)"
if make -C "$work" KDIR="$kdir" KBUILD_MODPOST_WARN=1; then
	echo "OK: $ref"
else
	echo "FAILED: $ref" >&2
	exit 1
fi

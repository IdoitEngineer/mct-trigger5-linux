#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Compile trigger5.ko against every kernel whose headers are installed on this
# machine. Kernels without headers and kernels below BUILD_EXCLUSIVE_KERNEL_MIN
# (from dkms.conf) are skipped. Root is not required.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/linux-trigger5"

min=$(sed -n 's/^BUILD_EXCLUSIVE_KERNEL_MIN="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' \
	"$src/dkms.conf" | tail -n1)
[ -n "$min" ] || min=0

version_ge() {
	# $1 = actual, $2 = minimum
	[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

work=$(mktemp -d "${TMPDIR:-/tmp}/trigger5-local.XXXXXX")
trap 'rm -rf "$work"' EXIT

printf '%-32s %-8s %s\n' "KERNEL" "HEADERS" "RESULT"
printf '%-32s %-8s %s\n' "--------------------------------" "--------" "------"

failures=0
tested=0
skipped=0

for kdir in /lib/modules/*; do
	ver=$(basename "$kdir")

	if [ ! -e "$kdir/build/Makefile" ]; then
		printf '%-32s %-8s %s\n' "$ver" "no" "skipped (headers not installed)"
		skipped=$((skipped + 1))
		continue
	fi

	if ! version_ge "$ver" "$min"; then
		printf '%-32s %-8s %s\n' "$ver" "yes" "skipped (< $min)"
		skipped=$((skipped + 1))
		continue
	fi

	d="$work/$ver"
	mkdir -p "$d"
	cp "$src/trigger5.c" "$src/Makefile" "$d/"

	log="$d/build.log"
	extra=()
	[ -e "$kdir/build/Module.symvers" ] || extra+=(KBUILD_MODPOST_WARN=1)

	if make -C "$d" KDIR="$kdir/build" "${extra[@]}" >"$log" 2>&1; then
		printf '%-32s %-8s %s\n' "$ver" "yes" "OK"
		tested=$((tested + 1))
	else
		printf '%-32s %-8s %s\n' "$ver" "yes" "FAILED"
		sed -n '1,200p' "$log" | grep -E 'error:|Error [0-9]' | head -n 10 || true
		failures=$((failures + 1))
	fi
done

echo
echo "compiled: $tested, skipped: $skipped, failed: $failures"

[ "$failures" -eq 0 ]

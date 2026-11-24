#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Build and install the trigger5 DKMS module for every installed kernel that
# has headers and is supported by BUILD_EXCLUSIVE_KERNEL_MIN in dkms.conf.
#
# Must run as root:
#   sudo ./scripts/dkms-install-local.sh [--force] [--dry-run]
#
# The DKMS source must already be registered (./install.sh or the .deb).

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/linux-trigger5"

min=$(sed -n 's/^BUILD_EXCLUSIVE_KERNEL_MIN="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' \
	"$src/dkms.conf" | tail -n1)
[ -n "$min" ] || min=0

force=0
dry_run=0
for arg in "$@"; do
	case "$arg" in
	--force) force=1 ;;
	--dry-run) dry_run=1 ;;
	*) echo "usage: $0 [--force] [--dry-run]" >&2; exit 2 ;;
	esac
done

if [ "$(id -u)" -ne 0 ] && [ "$dry_run" -eq 0 ]; then
	echo "Please run as root (sudo $0)." >&2
	exit 1
fi

version_ge() {
	[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

if ! dkms status -m trigger5 -v 1.0.4 2>/dev/null | grep -q .; then
	if [ -d /usr/src/trigger5-1.0.4 ]; then
		echo "Registering DKMS source from /usr/src/trigger5-1.0.4"
		[ "$dry_run" -eq 1 ] || dkms add -m trigger5 -v 1.0.4
	else
		echo "trigger5/1.0.4 is not registered." >&2
		echo "Run 'sudo ./install.sh' or install trigger5-dkms first." >&2
		exit 1
	fi
fi

printf '%-32s %s\n' "KERNEL" "RESULT"
printf '%-32s %s\n' "--------------------------------" "------"

failures=0
installed=0
skipped=0

for kdir in /lib/modules/*; do
	ver=$(basename "$kdir")

	if [ ! -e "$kdir/build/Makefile" ]; then
		printf '%-32s %s\n' "$ver" "skipped (headers not installed)"
		skipped=$((skipped + 1))
		continue
	fi

	if ! version_ge "$ver" "$min"; then
		printf '%-32s %s\n' "$ver" "skipped (< $min)"
		skipped=$((skipped + 1))
		continue
	fi

	status=$(dkms status -m trigger5 -v 1.0.4 -k "$ver" 2>/dev/null || true)
	if [ "$force" -eq 0 ] && printf '%s' "$status" | grep -q ': installed'; then
		printf '%-32s %s\n' "$ver" "already installed"
		skipped=$((skipped + 1))
		continue
	fi

	cmd=(dkms install -m trigger5 -v 1.0.4 -k "$ver")
	[ "$force" -eq 1 ] && cmd+=(--force)

	if [ "$dry_run" -eq 1 ]; then
		printf '%-32s %s\n' "$ver" "would run: ${cmd[*]}"
		continue
	fi

	if "${cmd[@]}" >/tmp/trigger5-dkms-$ver.log 2>&1; then
		if modinfo -k "$ver" trigger5 >/dev/null 2>&1; then
			printf '%-32s %s\n' "$ver" "installed"
			installed=$((installed + 1))
		else
			printf '%-32s %s\n' "$ver" "installed, but modinfo failed"
			failures=$((failures + 1))
		fi
	else
		printf '%-32s %s\n' "$ver" "FAILED (see /tmp/trigger5-dkms-$ver.log)"
		tail -n 10 "/tmp/trigger5-dkms-$ver.log" 2>/dev/null || true
		failures=$((failures + 1))
	fi
done

echo
echo "installed: $installed, skipped: $skipped, failed: $failures"

[ "$failures" -eq 0 ]

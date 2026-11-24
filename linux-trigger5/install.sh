#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Install the MCT Trigger 5 DRM driver via DKMS (Devuan / Debian, no systemd needed).
set -eu

VERSION=1.0.5
MODULE=trigger5

if [ "$(id -u)" -ne 0 ]; then
	echo "Please run this script with sudo." >&2
	exit 1
fi

cd "$(dirname "$0")"

KVERSION=$(uname -r)
KDIR="/lib/modules/$KVERSION/build"
if [ ! -d "$KDIR" ]; then
	echo "Kernel headers not found: $KDIR" >&2
	echo "Install them first: sudo apt install linux-headers-$KVERSION" >&2
	exit 1
fi

if ! command -v dkms >/dev/null 2>&1; then
	echo "dkms not found. Install it first: sudo apt install dkms" >&2
	exit 1
fi

SRC="/usr/src/$MODULE-$VERSION"
install -d "$SRC"
install -m 644 trigger5.c Makefile dkms.conf "$SRC/"

# Replace any previous registration so re-running this script is safe.
if dkms status -m "$MODULE" -v "$VERSION" 2>/dev/null | grep -q .; then
	dkms remove -m "$MODULE" -v "$VERSION" --all || true
fi

dkms add -m "$MODULE" -v "$VERSION"
dkms build -m "$MODULE" -v "$VERSION"
dkms install -m "$MODULE" -v "$VERSION"

# Devuan's kmod init script loads this list at boot. udev can also autoload
# the module from its USB modalias, but the explicit entry is harmless.
grep -qx "$MODULE" /etc/modules 2>/dev/null || echo "$MODULE" >> /etc/modules

modprobe "$MODULE"
echo
echo "Installed. Driver status:"
dkms status -m "$MODULE" -v "$VERSION" || true
echo
lsmod | grep "^$MODULE " || echo "warning: $MODULE is not loaded"
ls -l /sys/class/drm/ | grep -v -E '^total' || true

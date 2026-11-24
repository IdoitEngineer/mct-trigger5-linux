#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Remove the MCT Trigger 5 DRM driver installed by install.sh.
set -eu

MODULE=trigger5
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VERSION=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' "$SCRIPT_DIR/dkms.conf")
[ -n "$VERSION" ] || {
	echo "cannot determine version from $SCRIPT_DIR/dkms.conf" >&2
	exit 1
}

if [ "$(id -u)" -ne 0 ]; then
	echo "Please run this script with sudo." >&2
	exit 1
fi

modprobe -r "$MODULE" 2>/dev/null || true

dkms remove -m "$MODULE" -v "$VERSION" --all 2>/dev/null || true
rm -rf "/usr/src/$MODULE-$VERSION"

if [ -f /etc/modules ]; then
	sed -i "/^$MODULE\$/d" /etc/modules
fi

depmod -a
echo "Removed $MODULE."

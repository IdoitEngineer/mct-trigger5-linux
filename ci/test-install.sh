#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Install trigger5-dkms in a container and build it for the distro kernel
# headers that are installed there.
#
#   ci/test-install.sh <trigger5-dkms_*.deb>
#
# Containers share the host uname -r, so the package postinst is given a build
# tree for the host version, and the distro kernel is built explicitly with
# "dkms -k <header-version>".

set -euo pipefail

deb=${1:-}
if [ -z "$deb" ] || [ ! -f "$deb" ]; then
	echo "usage: $0 <trigger5-dkms_*.deb>" >&2
	exit 1
fi

builddir=$(ls -d /lib/modules/*/build 2>/dev/null | head -n1 || true)
if [ -z "$builddir" ]; then
	echo "no kernel headers installed in the container" >&2
	exit 1
fi
kver=$(basename "$(dirname "$builddir")")

echo "distro kernel headers: $kver"

# Let the package postinst find a build tree for the host uname -r.
hostk=$(uname -r)
if [ ! -e "/lib/modules/$hostk/build" ]; then
	mkdir -p "/lib/modules/$hostk"
	ln -sfn "$builddir" "/lib/modules/$hostk/build"
fi

echo "installing $deb"
dpkg -i "$deb"

echo "building for $kver"
dkms add -m trigger5 -v 1.0.3 2>/dev/null || true
dkms build -m trigger5 -v 1.0.3 -k "$kver"
dkms install -m trigger5 -v 1.0.3 -k "$kver" --force

echo "verifying"
modfile=$(ls -1 "/lib/modules/$kver/updates/dkms/trigger5.ko"* 2>/dev/null | head -n1 || true)
if [ -z "$modfile" ]; then
	echo "trigger5.ko not found under /lib/modules/$kver/updates/dkms/" >&2
	exit 1
fi
modinfo "$modfile" | sed -n '1,8p'
dkms status -m trigger5 -v 1.0.3 -k "$kver"

# The container only has headers, not the kernel modules package, so depmod
# may not create modules.dep here. The explicit-file check above is the
# meaningful verification in this environment.
depmod -a "$kver" 2>/dev/null || true

echo "OK: trigger5 installed for $kver"

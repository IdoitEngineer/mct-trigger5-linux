#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Jeringa
#
# Remove the MCT Trigger 6 userspace daemon (T6evdi) and its leftovers.
#
# The dongle in this project (0711:5805) is a Trigger 5 device. T6evdi only
# matches Trigger 6 devices (0711:56xx and HP 03f0:0182), so it can never
# drive it. The evdi kernel module itself is left installed: it is a normal
# DKMS package and may be used by other software.
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "Please run this script with sudo." >&2
	exit 1
fi

killall T6evdi 2>/dev/null || true
killall T6contrl 2>/dev/null || true

rm -f /usr/sbin/T6evdi /usr/sbin/T6contrl
rm -f /etc/systemd/system/T6evdi.service
rm -f /etc/systemd/system/T6poweron.service
rm -f /etc/systemd/system/T6powerdown.service

# "options evdi initial_device_count=2" creates two unused virtual DRM cards.
rm -f /etc/modprobe.d/evdi.conf

if [ -f /etc/modules ]; then
	sed -i '/^evdi$/d' /etc/modules
fi

echo "Removed the Trigger 6 userspace driver."
echo "evdi (kernel module + library) is still installed."

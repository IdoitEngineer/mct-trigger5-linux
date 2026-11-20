# MCT Trigger 5 Linux driver (0711:5805)

This directory contains the driver that actually matches the dongle attached to
this machine, plus the notes that explain why the vendor `ubuntu_T6_*.run`
installer cannot work with it.

See the [repository README](../README.md) for the overall layout of the repo,
how the adapter works, firmware notes and troubleshooting.

## Status: working (verified 2026-11-20)

- Host: Devuan 6 (excalibur), kernel 7.3.3, X11 + KDE, no systemd
- Device: `0711:5805`, VGA output, LG E2341 panel (1920x1080)
- Result: `card1-VGA-1` probes, EDID/DDC read returns the mode list, modeset
  and bulk frame transfer work; the panel shows the desktop.
- Installed with DKMS as `trigger5/1.0`; autoloads via the USB modalias in
  `modules.alias` and the `/etc/modules` entry, so it survives reboots and
  kernel upgrades (DKMS rebuilds it).
- KDE/KScreen saved the output layout (`VGA-1-1`, 1920x1080 at +3840+1080).

Windows 11 in a VM drives the same dongle through MCT's Indirect Display Driver
(`trgidd64.inf`, section `Trigger5_install22621.NT`), which confirms the device
is Trigger 5 and healthy, but is a different rendering path from this DRM/KMS
driver.

## Local changes relative to upstream v6

`trigger5.c` carries local fixes, captured in `trigger5-edid-fullframe.patch`:

1. **EDID is read once during probe and cached.** The adapter returns corrupt
   EDID replies (`checksum is invalid`) if the vendor read is issued while
   bulk frame transfers are running. `get_modes` now reuses the cached EDID
   instead of re-reading it, so a re-probe (e.g. when rotating the output) can
   no longer wipe the connector's mode list.
2. **A full frame is pushed after a modeset and whenever the framebuffer
   changes** (X allocates a new framebuffer for software rotation). Previously
   only damaged rects were sent, leaving stale rows on the panel.
3. **Damage is clipped to the visible source rect**, and the latched
   `needs_reset` flag is cleared after a successful mode re-initialisation.

These are local workarounds until the driver lands upstream.

## Why the vendor package does not work here

The dongle enumerates as:

```
0711:5805  Magic Control Technology Corp.  MCT USB3.0 External Graphics Device
```

The `ubuntu_T6_260223.run` installer ships `T6evdi`, whose device match is
(`main.c`, `is_T6dev()`):

```c
desc.idVendor == 0x0711 && (desc.idProduct & 0xff00) == 0x5600
```

`0x5805 & 0xff00 == 0x5800`, so the daemon never claims the device. It is not
just the ID: the protocols differ. `T6evdi` expects a bulk-IN endpoint at
`0x81` and an interrupt-IN endpoint at `0x83` (`t6.h`), while this dongle has a
bulk-OUT endpoint `0x01` and an interrupt-IN endpoint `0x84`.

`0x5805` is part of MCT's **Trigger 5** USB 3.0 family. Trigger 6 (T6) is the
`0x56xx` range, which is what the vendor `.run` targets.

## What is included

`trigger5.c` is the out-of-tree DRM/KMS driver for MCT Trigger 5 adapters
(`drivers/gpu/drm/tiny/trigger5.c`). Its USB ID table explicitly includes
`0711:5805`. It only needs the bulk-OUT endpoint and control transfers, which
matches this dongle's descriptors.

Source: `[PATCH v6] drm/tiny: trigger5: Add MCT Trigger 5 USB display driver`
by Ho Jie Feng, 2026-11-12, posted for review on dri-devel. The driver is not
in the mainline kernel yet, so it is built here as a DKMS module.

The driver implements modesetting, EDID retrieval and USB framebuffer
transfers. Upstream only the HDMI path has been tested; this dongle reports the
VGA group (no USB audio interface), so the VGA path is the less-travelled one.

## Install

```sh
cd ~/Projects/MCT_USB3.0_External_Graphics_Device/linux-trigger5
sudo ./install.sh
```

The script builds the module with DKMS against the running kernel's headers and
adds `trigger5` to `/etc/modules` (Devuan's kmod init script). No systemd is
required.

A Debian package is also available; build it with `make debpkg` (or
`dpkg-buildpackage -us -uc -b`) — see the
[repository README](../README.md#building-a-debian-package) — and install
`trigger5-dkms_1.0_all.deb`.

Manual build if you prefer:

```sh
make
sudo insmod trigger5.ko
```

## Verify

See [Testing the driver with xrandr](../README.md#testing-the-driver-with-xrandr)
in the repository README for output discovery, placement, rotation tests and
recovery commands.

```sh
dmesg | grep -i trigger5
lsmod | grep trigger5
ls /sys/class/drm/
xrandr --query            # X11
```

The dongle should appear as a new `card1`/`card1-VGA-1` (or `card1-HDMI-A-1`)
and show up in the desktop's display settings as an additional monitor.

## Optional: remove the wrong (Trigger 6) driver

```sh
sudo ./remove-t6-driver.sh
```

This deletes `T6evdi`/`T6contrl` and `evdi.conf` (which currently creates two
unused evdi DRM cards at boot). The `evdi-dkms` package itself is left in
place, since other software may use it.

## Uninstall

```sh
sudo ./uninstall.sh
```

## Notes

- Kernel **7.3 or newer** is required: the driver uses DRM APIs added in 7.3
  (`atomic_create_state`, `drm/clients/drm_client_setup.h`). `dkms.conf` sets
  `BUILD_EXCLUSIVE_KERNEL_MIN="7.3"`, so DKMS skips older installed kernels
  instead of failing their builds.
- The module is signed neither by DKMS (no MOK key configured) nor by the
  kernel; Secure Boot is disabled on this machine, so that is fine.
- Kernel upgrades are handled by DKMS automatically.
- If the display appears but stays black, test it on both an HDMI and a VGA
  output if the dongle has one; upstream VGA support is untested.
- Provenance detail: the patch was retrieved from the dri-devel archive, which
  soft-wrapped a few long lines and rewrote tabs as spaces. Those lines were
  rejoined and the obfuscated `MODULE_AUTHOR` address restored. The code is
  otherwise unmodified.

## License

The driver is GPL-2.0-only (see the SPDX header in `trigger5.c`). The
packaging, scripts and documentation in this directory are also
GPL-2.0-only. See [`../LICENSE`](../LICENSE) for the full text.

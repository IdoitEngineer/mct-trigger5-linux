# MCT USB 3.0 External Graphics Adapter — Linux (Devuan/Debian)

Working notes and driver for the **Magic Control Technology MCT USB3.0 External
Graphics Device** (`0711:5805`) on Devuan 6 / kernel 7.3.3.

The short version: the vendor's Linux installer in this directory is for the
**Trigger 6** generation and cannot drive this dongle. This repo contains the
**Trigger 5** DRM/KMS driver that does, packaged for DKMS, plus the notes that
explain why.

**License:** GPL-2.0-only for the project's own work (driver, packaging, docs);
the vendor binaries are proprietary and not covered — see
[Copyright and licensing](#copyright-and-licensing) and [LICENSE](LICENSE).

---

## Contents

| Path | What it is |
|---|---|
| `TriggerFamily-Dongle-25.01.1223.0179.exe` | MCT's Windows InstallShield package. Covers the whole Trigger family (T1P/T2/T3/T5/T6 INFs, an Indirect Display Driver, firmware update tools). Not used on Linux. |
| `TriggerFamily-Dongle-25.01.1223.0179.zip` | The same `.exe`, zipped. No separate sources inside. |
| `ubuntu_T6_260223.run` | MCT's Linux installer for **Trigger 6**: builds evdi + the `T6evdi`/`T6contrl` userspace daemon and installs systemd units. Does not match this dongle. |
| `linux-trigger5/` | The Trigger 5 DRM driver (`trigger5.c`), DKMS packaging, install/uninstall scripts, Debian packaging and driver notes. See [`linux-trigger5/README.md`](linux-trigger5/README.md). |
| `docs/usb-protocol.md` | USB protocol specification: descriptors, control requests, mode-set payload, bulk frame header and throughput maths. |
| `trigger5-dkms_1.0_all.deb` | Local build artifact (not tracked in git). Build it with the instructions below. |

## Status

- Device probes, reads EDID from the attached monitor, sets modes and streams
  frames. Verified with a VGA panel (`LG E2341`, 1920×1080) on 2026‑11‑20.
- Driver installed via DKMS as `trigger5/1.0`; autoloads from the USB modalias
  and `/etc/modules`, so it survives reboots and kernel upgrades.
- Local fixes for the EDID-read race and partial-frame updates are in
  `linux-trigger5/trigger5.c` (see below). Portrait rotation is expected to work
  after the patched module is loaded; if not, native plane rotation is the next
  step.

## Quick start

```sh
cd linux-trigger5

# 1. Build and install the DKMS module (needs kernel headers + dkms)
sudo ./install.sh

# 2. Reboot (or stop X, modprobe -r trigger5, modprobe trigger5, start X)
sudo reboot
```

After boot:

```sh
lsmod | grep trigger5
ls /sys/class/drm/                 # expect card1-VGA-1
cat /sys/class/drm/card1-VGA-1/status   # "connected"
DISPLAY=:0 xrandr --query | grep VGA
```

The output normally shows up as `VGA-1-1` in X11 (the `modesetting` driver
appends a provider suffix). Arrange it in KDE's *System Settings → Display and
Monitor*; KScreen persists the layout.

Optional cleanup of the wrong (Trigger 6) driver and its leftovers:

```sh
sudo ./remove-t6-driver.sh
```

Uninstall the Trigger 5 driver:

```sh
sudo ./uninstall.sh
```

## Building a Debian package

The packaging lives in `linux-trigger5/debian/` and produces a single
architecture-independent **DKMS source package** (nothing is compiled at
package-build time; DKMS compiles against the target kernel on install).

```sh
cd linux-trigger5
dpkg-buildpackage -us -uc -b
```

Result: `../trigger5-dkms_1.0_all.deb` (repo root), containing
`/usr/src/trigger5-1.0/{trigger5.c,Makefile,dkms.conf}`.

Install it:

```sh
sudo apt install ./trigger5-dkms_1.0_all.deb
# or
sudo dpkg -i trigger5-dkms_1.0_all.deb
```

The package's `postinst` runs `dkms add/build/install` for the running kernel
and `prerm` removes the module again. `apt`/`dpkg` will pull in `dkms`; kernel
headers still need to be installed (`linux-headers-$(uname -r)`).

Notes:

- **evdi is not required.** `trigger5` is a standalone DRM driver. The
  `evdi-dkms` package is only needed by MCT's Trigger 6 userspace daemon.
- If you installed the module earlier with `sudo ./install.sh`, remove it first
  (`sudo ./uninstall.sh`) to avoid two DKMS registrations for the same module.
- Build dependencies: `debhelper`, `dkms`, `dpkg-dev` (all present in a normal
  Devuan/Debian build environment).

---

## Why the vendor Linux package is not used

`ubuntu_T6_260223.run` ships `T6evdi`, whose device check is:

```c
desc.idVendor == 0x0711 && (desc.idProduct & 0xff00) == 0x5600
```

This dongle is `0x5805`, so `0x5805 & 0xff00 == 0x5800` does not match. It is
not only the ID: `T6evdi` expects a bulk-IN endpoint at `0x81` and an
interrupt-IN endpoint at `0x83`, while this adapter has a bulk-OUT `0x01` and an
interrupt-IN `0x84`.

- Trigger 6 (T6) = `0711:56xx`, JPEG-compressed stream, evdi userspace daemon.
- Trigger 5 = `0711:58xx`, raw RGB stream, DRM/KMS kernel driver.

The Windows package works on the same dongle because it is a family package:
Windows binds `trgidd64.inf` section `Trigger5_install22621.NT`, i.e. MCT's
Indirect Display Driver for Trigger 5.

---

## How the device works

### USB identity

```
VID:PID      0711:5805
Speed        USB 3.0 SuperSpeed (5 Gbps)
Class        ff / 10 / 00 (vendor specific)
Endpoints    0x01  bulk OUT, 1024-byte packets, burst 12
             0x84  interrupt IN, 8 bytes (not used by this driver)
Power        self-powered, 8 mA from the bus
```

### Data path

```
compositor (KWin) ──► DRM framebuffer (XRGB8888)
        │
        ▼
trigger5.ko: convert damaged region to RGB888
        │  20-byte header: magic 0xfb, x, y, width, height,
        │  payload length, frame counter, checksum
        ▼
128 KiB URBs (8 in flight) ──bulk OUT──► dongle RAM ──► panel
```

- The driver sends **uncompressed 24-bit RGB**, not JPEG (unlike Trigger 6).
- Only damaged rectangles are sent in steady state. After a modeset, a new
  framebuffer, or a re-init, a **full frame** is pushed (local fix, see below).
- The dongle buffers the frame in its own memory (Trigger 5 chips have 16 MB)
  and scans it out at up to 1080p60.

### Control path (vendor requests, EP0)

| Request | Code | Used for |
|---|---|---|
| `KEEPALIVE` | `0x91` | disarm keepalive at probe |
| `GET_REGISTER` / `SET_REGISTER` | `0xA5` / `0xC4` | register access during mode set |
| `GET_STATUS` | `0xA6` | monitor connected? |
| `GET_EDID` | `0xA8` | read EDID block from the monitor (DDC tunnel) |
| `SET_MODE` | `0xC3` | packed timing + PLL programming |
| `SET_CURSOR_POSITION` | `0xC8` | cursor position |
| `FIRMWARE_RESET` | `0xD1` | re-initialise on-device firmware |

Exact field layouts, the 35-byte `SET_MODE` payload and the 20-byte bulk header
are documented in [`docs/usb-protocol.md`](docs/usb-protocol.md).

### Frame buffer: on the device

The bulk header carries an offset and a rectangle, not a complete image, so the
adapter keeps a **full frame in its own RAM** and each transfer updates a region
of it. The display controller scans out from that memory at the programmed mode
rate, independently of USB throughput. Trigger 5 chips such as the T5-302 have
16 MB of frame memory; the exact size of the `0x5805` variant is not published.

Consequences:

- normal operation only sends damaged rectangles (cheap updates);
- a region that is never repainted stays on screen — the local fix forces one
  full-frame update after a modeset/new framebuffer.

### Throughput and realistic frame rate

| Quantity | Value |
|---|---|
| One full 1920×1080 RGB888 frame | 6,220,800 bytes (~5.93 MiB) |
| 60 fps of full frames | ~373 MB/s (~2.99 Gbit/s payload) |
| USB 3.0 SuperSpeed theoretical payload | ~500 MB/s (5 Gbit/s, 8b/10b) |
| Realistic link throughput (estimated) | ~400–480 MB/s |

So the link can carry roughly **50–60 full 1080p frames/s**; including URB
latency and the XRGB8888→RGB888 conversion, a realistic sustained full-frame
rate is about **45–55 fps**. That is borderline for full-screen 60 Hz motion
but more than enough for desktop use, where only damaged areas are transferred.
The driver rejects modes whose frame would exceed 8 MiB, so 1920×1080 fits while
2560×1440 and 4K do not.

## Firmware: on the dongle, not on the host

There is **no firmware blob in this repo or in the Linux driver**, and the
driver never calls `request_firmware()`.

- The application firmware is stored in the adapter (mask ROM plus serial
  flash/ROM) and runs as soon as the device is powered. The host driver only
  issues the `0xD1 FIRMWARE_RESET` control request to re-initialise it around
  mode changes.
- The firmware images in the Windows installer are **update payloads for other
  generations**, not boot-time blobs: `.t6img` files are Trigger 6 images,
  `MTrigger2.bin`/`MTrigger2.exe` are the Trigger II updater, and
  `Comm Trigger6` is the T6 communication component. MCT's T6 tool
  (`T6contrl`) contains the ROM read/write code that writes such images to the
  device's flash; there is no Trigger 5 update path in the Linux package.
- **EDID is not stored on the dongle.** It is read from the attached monitor
  over DDC through request `0xA8`. That is why a different monitor changes the
  mode list, and why the driver caches it (re-reading while streaming returns
  corrupt data).

---

## The driver

- Source: `linux-trigger5/trigger5.c`, the in-review mainline driver
  `drivers/gpu/drm/tiny/trigger5.c`, from `[PATCH v6] drm/tiny: trigger5`
  by Ho Jie Feng (dri-devel, 2026‑11‑12).
- Packaging: DKMS (`trigger5/1.0`) with a plain out-of-tree `Makefile`.
- Upstream tested only the HDMI path; this unit is the VGA path and works.
- **`evdi` is not required.** `trigger5` is a standalone DRM driver; the
  `evdi-dkms` package is only needed by MCT's Trigger 6 userspace daemon.

### Local changes (captured in `trigger5-edid-fullframe.patch`)

1. **EDID read once at probe, cached.** The adapter corrupts the EDID control
   reply if it arrives while bulk transfers are running (`EDID block 0 …
   checksum is invalid`). Re-reading on every `get_modes` wiped the connector's
   mode list and dropped the output to 1024×768 on any re-probe, e.g. rotation.
2. **Full frame after enable / framebuffer change.** Prevents stale rows when
   X allocates a new framebuffer for software rotation.
3. **Damage clipped to the visible source rect**, and the latched
   `needs_reset` flag cleared after a successful mode re-init.

These are local until the driver lands upstream.

### Rotation

X11/KWin rotate this output in software (the DRM plane does not advertise
rotation). With the fixes above the EDID survives and the panel gets a complete
repaint. If the picture is still geometrically wrong after that, the next step
is native plane rotation in the driver (rotation property + rotated conversion
and damage mapping).

---

## Testing the driver with xrandr

Run these as the logged-in desktop user (`DISPLAY=:0`), without sudo except
where noted.

### Discover the output

```sh
xrandr --listproviders
xrandr --query | sed -n '1p;/connected/p'
cat /sys/class/drm/card1-VGA-1/status          # "connected"
cat /sys/class/drm/card1-VGA-1/modes | head    # 1920x1080 ...
```

The kernel connector is `card1-VGA-1`; X11 names the same output `VGA-1-1`
(the modesetting driver appends a provider suffix, so do not be surprised if it
is plain `VGA-1` on another setup).

### Put it on the far left

```sh
xrandr --output VGA-1-1 --mode 1920x1080 --pos 0x0 \
       --output DP-0    --pos 1920x0 \
       --output DP-2.2  --pos 3840x0 \
       --output HDMI-0  --pos 1920x1080 \
       --output DP-5    --pos 3840x1080
```

Adjust the names/positions of the other outputs to match your machine. To move
only the new screen next to an existing one, use `--left-of <output>` instead
and let the server normalise the origin.

### Rotation smoke test

```sh
xrandr --output VGA-1-1 --rotate left        # 90° counter-clockwise
sleep 3
xrandr --query | grep VGA-1-1
cat /sys/class/drm/card1-VGA-1/modes | head -3
sudo dmesg | grep -i -E 'trigger5|edid' | tail -20
```

Pass criteria:

- `xrandr` reports `1080x1920 … left`;
- `1920x1080` is still in the kernel mode list;
- no `EDID block 0 … checksum is invalid` in `dmesg`;
- the panel shows a correctly rotated desktop, not stale/garbled rows.

Restore to landscape:

```sh
xrandr --output VGA-1-1 --rotate normal --mode 1920x1080 --pos 0x0
```

If the output drops to `1024x768`/`0mm x 0mm` (mode list lost), recover with:

```sh
xrandr --output VGA-1-1 --off
# replug the dongle, or: sudo modprobe -r trigger5 && sudo modprobe trigger5
xrandr --output VGA-1-1 --mode 1920x1080 --pos 0x0
```

### Quick regression checklist

- [ ] `card1-VGA-1` reports `connected`
- [ ] `xrandr` offers `1920x1080`
- [ ] rotate left, right and back to normal
- [ ] after each rotation the mode list is intact and `dmesg` is clean
- [ ] full-screen video/animation looks smooth
- [ ] after reboot the output returns in the saved layout

`xrandr` changes last only for the session. Apply the final layout in KDE's
*System Settings → Display and Monitor*; KScreen stores it under
`~/.local/share/kscreen/`.

## Troubleshooting

| Symptom | Check / fix |
|---|---|
| No `card1` after boot | `lsmod \| grep trigger5`; `sudo dmesg \| grep -i trigger5`; check `/etc/modules` contains `trigger5`. |
| Module won't unload | It is in use by X/DRM. Stop the display manager or reboot. |
| Panel stays black | Check `cat /sys/class/drm/card1-VGA-1/status`; try `xrandr --output VGA-1-1 --auto`. |
| Output falls back to 1024×768 / `0mm x 0mm` | EDID read failed. The patched driver caches the EDID; if it still happens, replug the dongle or `sudo modprobe -r trigger5 && sudo modprobe trigger5`. |
| `EDID … checksum is invalid` in dmesg | Same race; fixed by the cached-EDID change. |
| Garbled / stale picture after rotation | Fixed by the full-frame change; if it persists, native rotation is needed. |
| Layout lost after reboot | Apply it in KDE's display settings (KScreen), not with raw `xrandr`. |
| Kernel upgrade fails to rebuild | `sudo dkms autoinstall`; if the API changed, rebase `trigger5.c` on a newer upstream revision. |

Recovery if a rotation experiment leaves the output broken:

```sh
DISPLAY=:0 xrandr --output VGA-1-1 --off
# replug the dongle if the mode list is gone
DISPLAY=:0 xrandr --output VGA-1-1 --mode 1920x1080 --pos 0x0
```

---

## Tested environment

| Item | Value |
|---|---|
| OS | Devuan GNU/Linux 6 (excalibur), X11 + KDE, no systemd |
| Kernel | 7.3.3 |
| GPU / session | NVIDIA + KWin, Xorg via lightdm |
| Device | `0711:5805` MCT USB3.0 External Graphics Device |
| Panel | LG E2341, VGA, 1920×1080@60 |

## Copyright and licensing

This is general guidance, not legal advice.

### The driver: GPL-2.0-only

`trigger5.c` is licensed **GPL-2.0-only** and carries its SPDX header and
copyright notices (Ho Jie Feng and Linux kernel contributors). GPL obligations
apply if you redistribute it, modified or not:

- keep the copyright, SPDX and license notices;
- make the complete corresponding source available (this repo does, since the
  driver is shipped as source);
- license your modifications under GPL-2.0 as well — the local EDID/full-frame
  changes are derivative works;
- do not add restrictions on top of the GPL.

Kernel modules are generally treated as derivative works of the kernel, so
GPL compatibility is expected regardless.

Provenance: the copy here came from the public dri-devel patch series. The
mail-archive line wrapping and tab expansion were repaired, soft-wrapped lines
were rejoined, and an obfuscated `MODULE_AUTHOR` address was restored. The
functional changes are isolated in `linux-trigger5/trigger5-edid-fullframe.patch`.

### The vendor files: proprietary

`TriggerFamily-Dongle-…exe`, its `.zip`, and `ubuntu_T6_260223.run` are MCT
proprietary binaries with **no redistribution licence**. Specifically:

- the `.run` shell script has no licence header;
- the `T6evdi`/`T6contrl` sources inside it have no licence headers, so they
  default to "all rights reserved" (being distributed in an installer is not a
  grant to redistribute);
- the `evdi` source bundled inside the `.run` **is** GPL-2.0 (DisplayLink/Red
  Hat copyright) and would need normal GPL compliance if extracted and
  redistributed;
- the InstallShield copyright strings in the `.exe` belong to the installer
  tooling, not to MCT's driver.

**Recommendation before making this repo public:** do not publish the vendor
files. They are currently tracked in git.

```sh
git rm --cached TriggerFamily-Dongle-25.01.1223.0179.exe \
               TriggerFamily-Dongle-25.01.1223.0179.zip \
               ubuntu_T6_260223.run
printf '%s\n' 'TriggerFamily-*' '*.exe' '*.zip' 'ubuntu_T6_*.run' >> .gitignore
git commit -m "Do not track vendor binaries"
```

`git rm --cached` only stops tracking them in the current commit; the blobs
remain in history. To publish clean history, rewrite it first (destructive):

```sh
git filter-repo --invert-paths \
  --path TriggerFamily-Dongle-25.01.1223.0179.exe \
  --path TriggerFamily-Dongle-25.01.1223.0179.zip \
  --path ubuntu_T6_260223.run
```

Then point readers at MCT's own download page for the vendor installers instead
of mirroring them.

### Our work: GPL-2.0-only

The README, protocol notes, packaging, scripts and makefiles are original work
by Jeringa and are licensed **GPL-2.0-only**, the same licence as the driver.
The full text is in [LICENSE](LICENSE).

This does not change the vendor files: they remain proprietary, are not covered
by this licence, and should not be redistributed.

Everything here is provided as-is, with no warranty. Flashing, rotating and
modesetting USB display hardware can leave a session unusable — keep a second
display or SSH access handy.

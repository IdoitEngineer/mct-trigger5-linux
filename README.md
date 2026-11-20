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
| `linux-trigger5/` | The Trigger 5 DRM driver (`trigger5.c`), DKMS packaging, install/uninstall scripts, Debian packaging and driver notes. See [`linux-trigger5/README.md`](linux-trigger5/README.md). |
| `docs/usb-protocol.md` | USB protocol specification: descriptors, control requests, mode-set payload, bulk frame header and throughput maths. |
| `trigger5-dkms_1.0_all.deb` | Local build artifact (not tracked in git). Build it with the instructions below. |

Vendor installers are not kept in this repo (they are proprietary); see
[Vendor drivers](#vendor-drivers) for MCT's download links.

## Status

- Device probes, reads EDID from the attached monitor, sets modes and streams
  frames. Verified with a VGA panel (`LG E2341`, 1920×1080) on 2026‑11‑20.
- Driver installed via DKMS as `trigger5/1.0`; autoloads from the USB modalias
  and `/etc/modules`, so it survives reboots and kernel upgrades.
- Native plane rotation (0/90/180/270) is supported; rotated frames are
  converted in the driver and sent as full-frame updates.

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
ls /sys/class/drm/                 # cardN-VGA-1, N depends on probe order
cat /sys/class/drm/card*-VGA-1/status   # "connected"
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

# direct
dpkg-buildpackage -us -uc -b

# or through the Makefile (same command)
make debpkg
```

Both produce these artefacts one level up (repo root):

```
../trigger5-dkms_1.0_all.deb
../trigger5_1.0_amd64.changes
../trigger5_1.0_amd64.buildinfo
```

Remove the Debian build state and `.changes`/`.buildinfo` (the built `.deb`
stays):

```sh
make debclean
```

Install it:

```sh
sudo apt install ./trigger5-dkms_1.0_all.deb
# or
sudo dpkg -i trigger5-dkms_1.0_all.deb
```

Typical end-to-end:

```sh
cd linux-trigger5
make debpkg
sudo apt install ../trigger5-dkms_1.0_all.deb
sudo reboot
```

The package's `postinst` runs `dkms add/build/install` for the running kernel
and `prerm` removes the module again. `apt`/`dpkg` will pull in `dkms`; kernel
headers still need to be installed (`linux-headers-$(uname -r)`).

Notes:

- **evdi is not required.** `trigger5` is a standalone DRM driver. The
  `evdi-dkms` package is only needed by MCT's Trigger 6 userspace daemon.
- If you installed the module earlier with `sudo ./install.sh`, remove it first
  (`sudo ./uninstall.sh`) to avoid two DKMS registrations for the same module.
- Build dependencies: `debhelper`, `dpkg-dev`. `dkms` (>= 2.1) is needed at
  install time only; the package uses hand-written maintainer scripts, so it
  works with the stock Debian/Ubuntu `dkms` (no `dh_dkms` addon required).

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

## Vendor drivers

The vendor installers are **not** stored in this repo: they are MCT proprietary
binaries with no redistribution licence. Use MCT's own download links below.
Links were checked on 2026-11-20 (HTTP status and file name/size).

### Windows — Trigger family

- Download page: <https://www.mct.com.tw/download.php?lang=en&tb=1>
  (entry "USB Display Adapters (Windows 7/ 8.1/10 /11)")
- Direct link (serves `TriggerFamily-Dongle-25.01.1223.0179.zip`, 75,999,287 bytes):
  <https://www.mct.com.tw/include/download.php?dl=MjAxODAwNzczX158ZG93bmxvYWRefDAwMDAwMDAxNTA=>
- The zip contains `TriggerFamily-Dongle-25.01.1223.0179.exe` (InstallShield).
  On Windows 11 it installs `trgidd64.inf` and binds the
  `Trigger5_install22621.NT` section to `0711:5805`.

SHA-256 of the copies used for this project:

```
ce08ab1526f806e78bdc249f2cc639817dd880dba36216b2518c36a67911d72a  TriggerFamily-Dongle-25.01.1223.0179.zip
a838a5653952510eb8829b1fcfbc3c0a96aaa3d97b4115341127aca6ce8f0fa2  TriggerFamily-Dongle-25.01.1223.0179.exe
```

### Linux — Trigger 6 only (not for this adapter)

MCT's Linux package targets Trigger VI (T6-688SL), not the Trigger 5 chip in
this device. It is listed for reference and for anyone with a T6 dongle.

- Download page: <https://www.mct.com.tw/download.php?lang=en&tb=1>
  (entry "Ubuntu driver for USB 3.0 Display Adapters", dated 2026-02-23)
- Direct link (serves `Linux Driver_Ubuntu 24.04.4.pdf`, 132,588 bytes):
  <https://www.mct.com.tw/include/download.php?dl=MjAxODAwNzczX158ZG93bmxvYWRefDAwMDAwMDAxNzQ=>
- That PDF links `ubuntu_T6_260223.run` on MCT's SharePoint:
  <https://mctsharing-my.sharepoint.com/:u:/g/personal/mctsharing_mctsharing_onmicrosoft_com/IQB96jD4laVBTYU5KguRVolNAcK0nV1ndfDimx7-ippGcvk?e=ZMsrfU>
  (resolve to `…/Documents/Winnie/ubuntu_T6_260223.run`; the SharePoint share
  link may expire — the MCT download page is authoritative)
- Installation, per the PDF:

  ```sh
  chmod u+x ubuntu_T6_260223.run
  sudo ./ubuntu_T6_260223.run
  ```

- MCT notes: supports Trigger VI T6-688SL products only; no HDMI audio on Linux.
- SHA-256 of the copy used for this project:

  ```
  516fa2096296625d4bc9f64e78b49d586379cc51e88a41fcb98d085104c0d964  ubuntu_T6_260223.run
  ```

Community copies of the same driver, for fallback/reference:

- <https://github.com/ia-S-on/T6_evdi> — extracted MCT T6 driver, includes `ubuntu_T6.run`
- <https://github.com/rfxDarth/mobilepixels-linux-driver> — older `ubuntuT6_24011601.run`, plus fixes


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
- Requires Linux **6.12 or newer**. The driver carries compile-time guards for
  the 7.x DRM API changes (`struct drm_atomic_state` → `drm_atomic_commit` in
  7.2, `atomic_create_state` in 7.3). `dkms.conf` sets
  `BUILD_EXCLUSIVE_KERNEL_MIN="6.12"`, so DKMS skips older installed kernels
  instead of failing their builds.
- Upstream tested only the HDMI path; this unit is the VGA path and works.
- **`evdi` is not required.** `trigger5` is a standalone DRM driver; the
  `evdi-dkms` package is only needed by MCT's Trigger 6 userspace daemon.

### Local changes (captured in `trigger5-local.patch`)

1. **EDID read once at probe, cached.** The adapter corrupts the EDID control
   reply if it arrives while bulk transfers are running (`EDID block 0 …
   checksum is invalid`). Re-reading on every `get_modes` wiped the connector's
   mode list and dropped the output to 1024×768 on any re-probe.
2. **Full frame after enable, framebuffer change or rotation change.**
   Prevents stale rows when X allocates a new framebuffer or the plane rotation
   changes.
3. **Damage clipped to the visible source rect**, and the latched
   `needs_reset` flag cleared after a successful mode re-init.
4. **Native plane rotation (0/90/180/270).** The plane advertises DRM rotation;
   non-zero rotations are converted in the driver and sent as full frames.

These are local until the driver lands upstream.

### Rotation

The primary plane advertises `DRM_MODE_ROTATE_0/90/180/270`, so X11/KWin can
use DRM plane rotation instead of a software shadow. Non-zero rotations are
converted in the driver (XRGB8888 → RGB888) and sent as full-frame updates;
`rotate-0` keeps the damage-based fast path. Portrait therefore works, at the
cost of full-frame bandwidth while rotated.

---

## Testing the driver with xrandr

Run these as the logged-in desktop user (`DISPLAY=:0`), without sudo except
where noted.

### Discover the output

```sh
xrandr --listproviders
xrandr --query | sed -n '1p;/connected/p'
cat /sys/class/drm/card*-VGA-1/status          # "connected"
cat /sys/class/drm/card*-VGA-1/modes | head    # 1920x1080 ...
```

The kernel connector is `cardN-VGA-1` (the card number depends on probe order);
X11 names the same output `VGA-1-1`
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
cat /sys/class/drm/card*-VGA-1/modes | head -3
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

- [ ] `card*-VGA-1` reports `connected`
- [ ] `xrandr` offers `1920x1080`
- [ ] rotate left, right and back to normal
- [ ] after each rotation the mode list is intact and `dmesg` is clean
- [ ] full-screen video/animation looks smooth
- [ ] after reboot the output returns in the saved layout

`xrandr` changes last only for the session. Apply the final layout in KDE's
*System Settings → Display and Monitor*; KScreen stores it under
`~/.local/share/kscreen/`.

## Local testing

These helpers are for the machine you are working on. They are **not** used by
the Docker jobs or the GitLab pipeline.

### Compile against every installed kernel

```sh
./scripts/test-local-kernels.sh
```

Scans `/lib/modules/*`, skips kernels without headers and kernels below
`BUILD_EXCLUSIVE_KERNEL_MIN` (6.12), and builds `trigger5.ko` for the rest in a
temporary directory. Newly installed header packages are picked up
automatically.

### Build the package and lint it

```sh
./scripts/test-local.sh
```

Runs the kernel matrix, builds `trigger5-dkms`, runs lintian, then cleans the
Debian build state.

### Install the module for every installed kernel (root)

```sh
sudo ./scripts/dkms-install-local.sh            # skip already installed
sudo ./scripts/dkms-install-local.sh --force    # rebuild/reinstall
sudo ./scripts/dkms-install-local.sh --dry-run  # show what would happen
```

The DKMS source must already be registered (`sudo ./install.sh` or the `.deb`).
Kernels without headers are skipped with a message. Verify afterwards with:

```sh
dkms status -m trigger5
modinfo -k <kernel> trigger5
```

### One-off API-boundary check (linux-stable)

```sh
./scripts/test-kernel-ref.sh v7.0
./scripts/test-kernel-ref.sh v7.1
```

Creates a worktree under `~/Software/Linux/builds/<ref>`, prepares it with
`modules_prepare`, and builds the module. Developer check only; it is not part
of Docker or CI. (`~/Software/Linux/linux-stable` and the build root can be
overridden with `LINUX_STABLE` and `BUILD_ROOT`.)

## Distribution testing with Docker

Only the **stock default kernels** of the distributions are tested:

- Debian 13 (trixie): 6.12.x
- Ubuntu 26.04 LTS: 7.0.x
- optional/manual: Ubuntu 26.10 (7.3.x)

```sh
./docker/run-tests.sh            # both stock targets
./docker/run-tests.sh debian
./docker/run-tests.sh ubuntu
```

Each run builds the image, compiles the module against the distro kernel
headers, builds and lints the `.deb`, installs it, and builds/verifies the DKMS
module. Containers share the host kernel, so the distro kernel build is done
with an explicit `dkms -k <header-version>`; the package `postinst` is exercised
against the host version with a symlinked build tree.

## CI

`.gitlab-ci.yml` runs the same stock-distribution checks on the GitLab Docker
runners:

| Job | Image | Kernel |
|---|---|---|
| `lint:debian` | debian:trixie | — (builds and lints the `.deb`) |
| `build:debian-6.12` | debian:trixie | 6.12.x |
| `build:ubuntu-7.0` | ubuntu:26.04 | 7.0.x |
| `test:install-debian-6.12` | debian:trixie | DKMS install + verification |
| `test:install-ubuntu-7.0` | ubuntu:26.04 | DKMS install + verification |
| `build:ubuntu-7.3` | ubuntu:26.10 | 7.3.x (manual, non-blocking) |

## Troubleshooting

| Symptom | Check / fix |
|---|---|
| No `card1` after boot | `lsmod \| grep trigger5`; `sudo dmesg \| grep -i trigger5`; check `/etc/modules` contains `trigger5`. |
| Module won't unload | It is in use by X/DRM. Stop the display manager or reboot. |
| Panel stays black | Check `cat /sys/class/drm/card*-VGA-1/status`; try `xrandr --output VGA-1-1 --auto`. |
| Output falls back to 1024×768 / `0mm x 0mm` | EDID read failed. The patched driver caches the EDID; if it still happens, replug the dongle or `sudo modprobe -r trigger5 && sudo modprobe trigger5`. |
| `EDID … checksum is invalid` in dmesg | Same race; fixed by the cached-EDID change. |
| Garbled / stale picture after rotation | Native rotation sends full frames. Capture `dmesg` and check the EDID/mode list; report the rotation value used. |
| Layout lost after reboot | Apply it in KDE's display settings (KScreen), not with raw `xrandr`. |
| Kernel upgrade fails to rebuild | `sudo dkms autoinstall`; if the API changed, rebase `trigger5.c` on a newer upstream revision. |
| `Bad return status for module build on kernel < 6.12` | Expected: the driver needs kernel ≥ 6.12. `dkms.conf` declares `BUILD_EXCLUSIVE_KERNEL_MIN="6.12"`, so DKMS skips those kernels. Refresh the installed source (`sudo apt install --reinstall ./trigger5-dkms_1.0_all.deb`, or `sudo ./install.sh`). |

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
functional changes are isolated in `linux-trigger5/trigger5-local.patch`.

### Our work: GPL-2.0-only

The README, protocol notes, packaging, scripts and makefiles are original work
by Jeringa and are licensed **GPL-2.0-only**, the same licence as the driver.
The full text is in [LICENSE](LICENSE).

This does not change the vendor files: they remain proprietary, are not covered
by this licence, and should not be redistributed.

Everything here is provided as-is, with no warranty. Flashing, rotating and
modesetting USB display hardware can leave a session unusable — keep a second
display or SSH access handy.

## Tested kernels and distributions

Last verified: 2026-11-20.

| Environment | Kernel | Verified |
|---|---|---|
| Devuan 6 host (real hardware) | 6.12.117, 7.2.12, 7.2.14, 7.3.3 | compile and DKMS install for each; runtime display on 7.3.3 |
| linux-stable API checks (host, manual) | v7.0, v7.1, v7.2, v7.3 | compile-only (`scripts/test-kernel-ref.sh`) |
| Debian 13 trixie (Docker + CI) | 6.12.x | module compile, `.deb`, DKMS install, verification (CI job passed) |
| Ubuntu 26.04 LTS (Docker + CI) | 7.0.x | module compile, `.deb`, DKMS install, verification (CI job passed) |
| Ubuntu 26.10 (optional CI job) | 7.3.x | compile |

GitLab pipeline `420` passed on 2026-11-20 for commit `eb8d64b` (lint, Debian
6.12 build + DKMS install, Ubuntu 7.0 build + DKMS install).

Minimum supported kernel: **6.12**. Stock distribution defaults below that are
not supported (Debian 12 / 6.1, Ubuntu 24.04 GA / 6.8); DKMS skips them via
`BUILD_EXCLUSIVE_KERNEL_MIN`.

Per-kernel installation on a developer machine:

```sh
sudo ./scripts/dkms-install-local.sh
dkms status -m trigger5
```


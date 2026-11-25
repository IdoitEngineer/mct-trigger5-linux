# AGENTS.md — MCT Trigger 5 Linux driver

## Layout and boundaries

- Git repository root is this directory. The parent workspace also holds untracked vendor installers and `dmesg-err.txt`; do not edit or commit those.
- `linux-trigger5/trigger5.c` — DRM/KMS driver, an out-of-tree copy of upstream `[PATCH v6] drm/tiny: trigger5`.
- `linux-trigger5/debian/` — DKMS-source package; it ships source only, not a compiled module.
- `linux-trigger5/tools/` — `trigger5-tool`, a libdrm/libusb probe and KMS tester.
- `docs/usb-protocol.md` — vendor request, mode-set payload and bulk-header reference.
- `.gitlab-ci.yml`, `ci/`, `docker/`, `scripts/` — CI and local verification.

## Branches and commits

- `master` is the only branch and tracks `origin/master`; use a feature branch for larger work.
- **ALWAYS ask before any `git push`.** Never treat a previous approval as standing permission for a later push, and never disable or override this rule.
- Do not force-push. Use `--force` only when explicitly requested.
- Push branches with `git push --no-follow-tags`; tag pushes trigger the Package Registry/Release jobs.
- Never commit build artefacts (`*.ko`, `*.o`, `.deb`, `linux-trigger5/tools/trigger5-tool`); `.gitignore` covers most.
- Never hand-edit the current version. Run `scripts/bump-version.sh X.Y.Z`; `VERSION` is the source of truth and `dkms.conf`, the Debian files and `DRIVER_PATCHLEVEL` must match it.
- Keep `SPDX-License-Identifier: GPL-2.0-only` headers. Vendor binaries are proprietary and untracked.

## Verification (there is no unit-test suite)

Run from the repository root:

- `./scripts/test-local.sh` — compile every installed header-bearing kernel, build the `.deb`, run `lintian`, then clean. Standard local gate.
- `./scripts/test-local-kernels.sh` — kernel compile matrix only.
- `./docker/run-tests.sh [debian|ubuntu]` — stock-distro tests: Debian 13 / 6.12 and Ubuntu 26.04 / 7.0. Requires Docker.
- `make -C linux-trigger5/tools` — build the tool; needs `build-essential`, `pkg-config`, `libdrm-dev`, `libusb-1.0-0-dev`.
- `linux-trigger5/tools/trigger5-tool probe` — read-only, safe while X runs.
- `sudo linux-trigger5/tools/trigger5-tool test --landscape` — KMS test; needs DRM master, so use a text console or stop the display manager.
- `./scripts/test-kernel-ref.sh v7.3` — optional API-boundary check against linux-stable; expects `~/Software/Linux/linux-stable` and `~/Software/Linux/builds` (override with `LINUX_STABLE` / `BUILD_ROOT`).
- `sudo ./scripts/dkms-install-local.sh [--force]` — install for all local kernels. `--force` only when requested.

CI tests only the **stock distribution default kernels**. Local work must still compile against every installed header-bearing kernel; do not narrow the matrix.

## Driver constraints

- Supports Linux **6.12+** (`BUILD_EXCLUSIVE_KERNEL_MIN` in `dkms.conf`). `trigger5.c` has guards for 6.12 and the 7.x API changes (7.2 `struct drm_atomic_commit`, 7.3 `atomic_create_state`). Do not remove them, and do not validate only against `uname -r`.
- `trigger5.c` is a modified upstream v6 copy. The local changes are summarised in the READMEs; the tree is maintained directly, with no in-tree patch file. Keep the upstream v6 source or link elsewhere if you need to rebase it later.
- EDID is read once at probe and cached. Never re-read EDID while bulk transfers are active: the adapter returns corrupt data and the connector loses its mode list.
- The adapter's `0xA4` supported-mode table is read once at probe and exposed read-only as `default_modes` on the card device's sysfs node. Never re-read `0xA4` while streaming; it is informational and the monitor EDID is authoritative.
- Rotation 90/180/270 is converted in the driver. Damage clips are mapped into panel space and sent as partial destination frames; full frames are still sent after enable, framebuffer changes and rotation changes. Xorg 1.21's modesetting driver ignores plane rotation and rotates in software; that is what `full_frame_period_ms` is for. Read the root README before changing damage/full-frame logic.
- Linux sends raw RGB888 (`flags=0x01`). The Windows Trigger 5 driver contains a proprietary `EvoEncode` encoded-frame path (`flags=0x71` observed in captures); the payload is not decoded or implemented here. Do not claim compressed-frame support.
- A USB transfer error latches `needs_reset` and stops I/O. Do **not** reintroduce `usb_queue_reset_device()`: it has taken down the whole xHCI controller. Recovery is replug or unbind/rebind.
- Hardware cursor: the visible cursor is uploaded by the Windows driver as a **bulk type-2 packet** (64×64 ARGB, 16 KiB, then show/move/hide packets). The `0xC8` control writes (`0xE868` enable, `wValue=X`/`wIndex=Y` position, negative `value - 1`) are secondary. Keep the cursor plane 64×64 ARGB8888 and compatible with `drmModeSetCursor2`; implement the bulk path before claiming cursor support. See the private `windows-RE-trigger/CURSOR-BULK-PACKETS.md` notes (not shipped).

## Hardware and runtime gotchas

- The device is `0711:5805`, a **Trigger 5**. The vendor `ubuntu_T6_*.run` / `T6evdi` targets Trigger 6 (`0711:56xx`) and never matches; `evdi` is not required.
- Card numbers and DRM connector IDs change between boots. Use `/sys/class/drm/card*-VGA-1` or find the card by DRM driver name `trigger5`; never hardcode `card0` or `VGA-1`.
- In X11 the output usually appears as `VGA-1-1` (modesetting provider suffix).
- `/sys/kernel/debug` is not mounted by default on Devuan. Mount it before reading DRM state:
  `sudo mount -t debugfs debugfs /sys/kernel/debug`, then `sudo cat /sys/kernel/debug/dri/*/state`.
- `trigger5-tool probe` is read-only; `test` and `cursor` need DRM master and cannot run while X owns the card. The tool must not issue vendor USB requests; the kernel driver owns the interface.
- After a USB error the adapter can stop responding. `modprobe -r trigger5` may fail while X uses it; replug the dongle or stop X first.

## Current branch traps (verify before trusting docs)

- The adapter **STALLs the `0xC8` cursor enable** (`wIndex=0xE868`, `-EPIPE`), but that is expected. The visible cursor is a 64×64 ARGB bitmap uploaded in a **bulk type-2 packet** (command 0), followed by type-2 show/move (command 3) and hide (command 5) packets. The Linux driver implements this bulk path (plus the `0xC8` writes); the cursor is hardware-verified at 1920x1080. See the private `windows-RE-trigger/CURSOR-BULK-PACKETS.md` notes (not shipped).
- `trigger5-tool --portrait-left/right` now draws the card in **framebuffer orientation**, so the driver's plane rotation produces a genuine portrait bitmap (sideways on a landscape panel, upright on a rotated monitor).
- 1920×1080 and 1024×768 are validated desktop modes. 1024×768 was verified through both `trigger5-tool --device-mode 3` and KDE/xrandr. The earlier blank-panel report was the adapter reset/settling path, not compression. The `0xD1 0x0001` display-disable has been removed.
- `trigger5-tool` restores the saved mode/FB/rotation atomically. If `display_restore()` fails it retries once; on continued failure it leaves the test framebuffer allocated rather than tearing down one the CRTC may still scan out.

## Packaging and CI

- `make -C linux-trigger5 debpkg` produces `trigger5-dkms_1.0.7_all.deb` at the repository root; `make debclean` removes package build state but leaves the `.deb`.
- The package is DKMS-source only (`Architecture: all`). Keep the hand-written `debian/trigger5-dkms.postinst` and `prerm`: stock Debian/Ubuntu `dkms` lacks the `dh_dkms` addon.
- `lintian` must stay clean; the `lint:debian` CI job builds and lints the package.
- Containers share the host `uname -r`. `ci/test-install.sh` deliberately symlinks the host build tree and builds the distro kernel explicitly with `dkms -k <header-version>`; preserve that when editing CI.
- GitLab jobs use the `tls-docker-runner` tag. Ubuntu 26.10 / 7.3 is a manual, non-blocking build.
- Tag pipelines run `publish:package` (generic Package Registry) and `publish:release` (GitLab Release with the `.deb` asset link). The version in the tag must match the `.deb` filename.

## Docs

- `README.md` — install, rotation/cursor workarounds, local/Docker/CI, tested kernels.
- `linux-trigger5/README.md` — driver notes and module parameters.
- `linux-trigger5/tools/README.md` — tool options and troubleshooting.
- `docs/usb-protocol.md` — request codes, mode-set payload, bulk header.
- `docs/call-flow.md` — X11/tool → DRM → driver → USB → panel call and data flow.
- `CHANGELOG.md` — release history and known limitations.
- `VERSION` / `scripts/bump-version.sh` — version source and updater.
- `windows-RE-trigger/` — private Ghidra export of the vendor Windows driver (ignored; proprietary vendor-derived material; not shipped).

## Agent skills

### Issue tracker

Issues and specs live in GitLab Issues on `gitlab.beltarc.org/jeringa/mct-trigger5-linux`. Use the GitLab REST API with the project token in `.git-token` (never print it); `glab` may be stale. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `GLOSSARY.md` and `docs/adr/`, created lazily when terms or decisions are resolved. See `docs/agents/domain.md`.

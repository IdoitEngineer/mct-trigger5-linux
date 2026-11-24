# Changelog

All notable changes to this repository are documented here. The driver is
currently distributed as the DKMS package `trigger5/1.0.x`; the Linux DRM
version follows the same `major.minor.patch` values.

## [1.0.6] - 2026-11-24

### Added

- `trigger5-tool probe`: print the adapter's own `0xA4` supported-mode table as
  "device default modes" after the monitor EDID modes, including JSON output.
- Driver: read the `0xA4` table once at probe and expose it read-only as
  `default_modes` on the card device's sysfs node.
- `VERSION` file and `scripts/bump-version.sh` to update the driver, DKMS,
  Debian and documentation version strings in one step.

### Changed

- Removed `linux-trigger5/trigger5-local.patch`; the tree is maintained
  directly and the local changes are documented in the READMEs.
- `ci/test-install.sh`, `scripts/dkms-install-local.sh`, `install.sh` and
  `uninstall.sh` now derive the version instead of hard-coding it.
- Documentation describes per-clip rotation damage and the measured bandwidth
  (2–4 Mbit/s damaged vs 12–18 Mbit/s full-frame).
- Tag pipelines publish the `.deb` to the generic Package Registry and attach
  it to a GitLab Release.

### Fixed

- `trigger5-tool` retries `display_restore()` once and leaves the test
  framebuffer allocated if the restore still fails, instead of tearing down a
  framebuffer the CRTC may still scan out.

## [1.0.5] - 2026-11-24

### Added

- Partial damage updates under plane rotation. Portrait modes map each
  framebuffer damage rectangle into panel space, align it to 4 pixels there,
  coalesce overlapping or vertically adjacent rectangles, and send one bulk
  header and RGB888 payload per remaining rectangle.

### Changed

- Rotation no longer forces a complete frame on every commit. Full frames are
  still sent after enable, framebuffer changes and rotation changes.
- `trigger5-tool` no longer turns portrait animation into a full-framebuffer
  damage clip, so the rotated modes exercise the per-clip path.

## [1.0.4] - 2026-11-23

### Changed

- Landscape partial updates now walk the DRM damage iterator instead of calling
  `drm_atomic_helper_damage_merged()`. Each clip is clipped to the visible
  source rectangle and aligned to a 4-pixel boundary, then sent as its own bulk
  header and RGB888 payload.
- Clips that overlap or form vertically adjacent bands are coalesced before
  sending, so disjoint dirty regions no longer inflate into one screen-sized
  bounding box.

### Fixed

- Animated landscape updates cover only the dirty regions rather than the
  bounding box of every clip.

## [1.0.3] - 2026-11-23

### Fixed

- Landscape partial updates no longer corrupt the panel. The adapter requires
  damage rows to start and end on a 4-pixel boundary; 1- and 2-pixel
  alignment corrupts the image, so the driver now expands the merged damage
  rectangle outwards and clamps it to the visible source area.
- Rotated output is unaffected because it already sends complete frames on
  every update.

### Changed

- `trigger5-tool` no longer pre-aligns damage rectangles by default, so the
  landscape animation exercises the driver's alignment. `--damage-align N`
  remains available for experiments.

## [1.0.2] - 2026-11-23

### Added

- `trigger5-tool`: separate framebuffer-oriented test cards for
  `--portrait-left` and `--portrait-right`.
  - left: blue accent, left-pointing arrow, `ROTATE 90 CCW`;
  - right: orange accent, right-pointing arrow, `ROTATE 270 CW`;
  - landscape and upside-down have their own accent colours.
- `CHANGELOG.md` (this file).

### Changed

- Portrait test cards are drawn directly in framebuffer orientation instead of
  pre-rotating a landscape card, so the driver's plane rotation is what
  produces the visible rotation.
- `trigger5-tool` footer reports the requested plane rotation separately from
  the canvas drawing rotation.
- Documentation updated for the two portrait cards and the verified cursor
  path.

## [1.0.1] - 2026-11-23

### Added

- Bulk type-2 cursor protocol in `trigger5.ko`:
  - command 0: upload a 64×64 ARGB bitmap in a 16 KiB payload;
  - command 3: show/move;
  - command 5: hide;
  - bitmap cached by DRM framebuffer ID.
- `docs/call-flow.md`: X11/tool → DRM/KMS → driver → USB → panel call and data
  flow.
- Reverse-engineering export of the vendor Windows driver in the separate
  `windows-RE-trigger` repository, including function maps, reconstructed
  protocol paths and extracted cursor bitmaps.
- `AGENTS.md` and engineering-skill configuration for future sessions.

### Changed

- `0xC8` cursor enable/position is retained as a secondary path. The adapter
  STALLs the enable; the driver latches the attempt and continues with
  positions, matching the Windows driver.
- The DRM cursor framebuffer is read through the shadow-plane mapping and
  uploaded only when the framebuffer changes.
- The `0xD1 0x0001` display-disable request was removed. It does not exist in
  the Windows driver and wedged the adapter on mode changes.
- Added the Windows-mode-set settle delays: 100 ms after stopping I/O before
  `reset0`, and 100 ms between `SET_MODE` and `reset201`.
- URB completions are reinitialised after `trigger5_stop_io()`, preventing a
  burst of stale completion credits after a mode change.
- The expected `-ENODEV` submit error during disconnect is suppressed.
- Removed the temporary mode-index parameters and per-step mode-set
  diagnostics.

### Fixed

- Hardware cursor is now visible and movable at 1920×1080.
- Unit `1.0.1` builds against Linux 6.12, 7.2 and 7.3; the DKMS package is
  lintian-clean.

### Known limitations

- 1920×1080 is the only validated output mode. 1024×768 mode-set packets are
  accepted but the panel stays blank; the Windows driver uses compressed
  frames (`flags=0x71`) there, while Linux sends raw RGB888 (`flags=0x01`).
- `trigger5-tool` still destroys the test framebuffer if `display_restore()`
  fails. Fixed in 1.0.6.

## [1.0.0] - 2026-11-20

### Added

- Initial DKMS packaging of the out-of-tree MCT Trigger 5 DRM/KMS driver for
  `0711:5805`.
- EDID cached at probe time to avoid corrupt reads during streaming.
- Full-frame refresh after enable, framebuffer changes and rotation changes.
- Damage tracking clipped to the visible source rectangle.
- Native plane rotation 0/90/180/270 with software conversion in the driver.
- Runtime `full_frame_always` and `full_frame_period_ms` parameters for X11
  software rotation.
- DRM cursor plane using the adapter cursor path.
- `trigger5-tool` with `probe`, `test`, and `cursor` commands.
- Debian DKMS-source packaging, Docker tests and GitLab CI for stock Debian 13
  and Ubuntu 26.04 kernels.

### Notes

- The adapter does not require `evdi`; the vendor Trigger 6 userspace driver
  does not match this device.
- Kernel support starts at 6.12 (`BUILD_EXCLUSIVE_KERNEL_MIN`).

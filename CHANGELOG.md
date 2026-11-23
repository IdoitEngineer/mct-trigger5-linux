# Changelog

All notable changes to this repository are documented here. The driver is
currently distributed as the DKMS package `trigger5/1.0.x`; the Linux DRM
version follows the same `major.minor.patch` values.

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
  fails.

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

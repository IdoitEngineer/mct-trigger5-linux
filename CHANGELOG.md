# Changelog

All notable changes to this repository are documented here. The driver is
currently distributed as the DKMS package `trigger5/1.0.x`; the Linux DRM
version follows the same `major.minor.patch` values.

## [1.0.13] - 2026-11-28

### Documented

- GNOME Wayland cannot remove a GPU at runtime. Mutter keeps the old GPU in
  its list; if a replugged adapter reuses the same `/dev/dri/cardN` path,
  `gnome-shell` logs `device already present` and can SIGSEGV (observed on
  Ubuntu 26.04 / GNOME 50.1). On Ubuntu 24.04 / GNOME 46 the shell can instead
  log GLib criticals and leave the new output disabled. **Log out before
  unplugging or replugging on Wayland**; a fresh session handles the new card
  normally. This is a Mutter limitation, not a driver change.
- Card and DRM connector numbers can change after a replug (`VGA-1` can become
  `VGA-2`). Use `card*-VGA-*` or find the card by its `trigger5` driver instead
  of hardcoding `VGA-1`.
- Verified on Ubuntu 24.04: after a fresh login with the adapter attached, the
  extra screen is available again.

## [1.0.12] - 2026-11-28

### Fixed

- Do not expose a connected connector without a valid EDID. The DRM no-EDID
  fallback could enable a 1024×768 stream, after which the EDID and status
  control requests returned corrupt data or timed out, and the output stayed
  without real modes. The connector now reports disconnected until an EDID is
  readable; the driver retries the DDC read while idle and switches to
  connected, with a hotplug event, once it succeeds.
- Retry the EDID read a few times immediately. The adapter occasionally
  returns a corrupt block (bad checksum); a single transient read no longer
  leaves the connector without modes until the next poll.
- Do not issue an EDID read from `get_modes` while the adapter is streaming,
  as a defence in depth against the corrupt-control-reply race.
- Retry the post-mode reset (`0xD1 0x0201`) up to five times instead of
  failing the whole modeset on a single timeout.
- Probe the EDID even when `GET_STATUS` reports disconnected or times out; that
  status bit is not reliable on this adapter.

### Changed

- The 1024×768 no-EDID fallback is no longer exposed. An adapter whose EDID
  cannot be read stays disconnected and has no output until a read succeeds.

## [1.0.11] - 2026-11-28

### Added

- The DKMS post-install and `scripts/dkms-install-local.sh` now replay the USB
  add event (`udevadm trigger`, scoped to vendor `0711`) after a successful
  install, so an adapter that is already attached binds without a reboot.
- EDID retry: the driver retries for up to 2 s at probe, then retries from the
  connector poll whenever no frame stream is active. A successful late read
  schedules a DRM hotplug event so X11 and Wayland re-enumerate the modes
  without a session restart.

### Fixed

- Serialise the EDID cache with a mutex.
- Harden USB disconnect: tolerate a missing interface data pointer, cancel the
  EDID hotplug work, clear the interface data, and stop the stream before
  freeing URBs and the shadow buffer.
- Guard suspend/resume against a disconnected interface.

### Documented

- X11 runtime unplug requires a patched `xorg-server` (upstream commits
  `dbcd36b` and `9babe7e7`; Ubuntu 24.04 has them in
  `21.1.12-1ubuntu1.9`). Debian 13 and Devuan 6 ship older X servers that can
  crash in `DGACloseScreen()` when a runtime-added GPU screen is removed.
  Wayland is not affected.

## [1.0.10] - 2026-11-28

### Fixed

- Initialise atomic CRTC and connector state on kernels before 7.3. The
  driver only provided `atomic_create_state`, which does not exist before
  7.3, and `drm_mode_config_reset()` only calls `.reset`; the state therefore
  stayed `NULL` after probe and the first commit warned in
  `drm_atomic_helper_crtc_duplicate_state()` (`WARN_ON(!crtc->state)`).
  Provide `.reset = drm_atomic_helper_crtc_reset` and
  `.reset = drm_atomic_helper_connector_reset` for kernels before 7.3,
  keeping `atomic_create_state` on 7.3 and later. This fixes the
  `output_poll_execute` / fbdev hotplug warning reported on Ubuntu 24.04
  and 26.04 with kernel 7.0 (issues #5 and #6).

## [1.0.9] - 2026-11-26

### Fixed

- Build on Linux 7.0. `drm_gem_fb_create_with_funcs()` has taken the
  `drm_format_info` argument since 7.0, not 7.2; the framebuffer-create guard
  now uses `>= 7.0`. This fixes the Ubuntu 26.04 / 7.0 build and DKMS install.

## [1.0.8] - 2026-11-25

### Added

- `self_damage` module parameter: the driver keeps a shadow copy of the last
  frame sent and compares each incoming framebuffer against it, sending only
  changed 64×64 tiles. The framebuffer `.dirty` callback drops userspace damage
  clips in this mode, which avoids the invalid clips Xorg produces under
  software rotation.
- Self-damage uses the existing 4-pixel alignment and per-rect send paths,
  ignores framebuffer identity (so page flips do not force full frames), and
  falls back to a complete frame when the shadow is missing, the framebuffer
  geometry changes, or more than 32 tiles change.
- `trigger5-tool params` lists, reads and writes the driver module parameters
  (`self_damage`, `full_frame_always`, `full_frame_period_ms`).

### Notes

- Opt-in; the default remains the userspace damage-clip path.
- Costs about 8.3 MB at 1920×1080 and one frame scan per commit. Idle desktop
  traffic should drop substantially.
- Self-damage is hardware-verified in rotate-0 (X11 landscape and portrait)
  and implemented for native rotated modes; native-rotation hardware
  validation is still pending. The custom `.dirty` callback covers Xorg's
  `drmModeDirtyFB` path; atomic clients that set `FB_DAMAGE_CLIPS` directly
  bypass it.

## [1.0.7] - 2026-11-25

### Changed

- Corrected the 1024×768 status and compression description: 1024×768 is
  validated through `trigger5-tool --device-mode 3` and KDE/xrandr, and Linux
  sends raw RGB888. The Windows Trigger 5 driver has a proprietary
  `EvoEncode` encoded-frame path that is not implemented here.
- Corrected the README rotation status text to describe partial damage frames.
- Agent documentation now requires `--no-follow-tags` for branch pushes and
  uses the project token for GitLab API access.
- Shared the 0xA4 table count clamp and the CEA/CVT mode synthesis helper.
- Replaced dead links to the private reverse-engineering notes.

### Fixed

- `scripts/bump-version.sh` now updates `DRIVER_MAJOR` and `DRIVER_MINOR` as
  well as the patch level, and preserves the mode of the files it edits.
- Maintainer scripts keep their executable bits.

### Notes

- Documentation and maintenance release; driver and tool behaviour is
  unchanged.

## [1.0.6] - 2026-11-24

### Added

- `trigger5-tool probe`: print the adapter's own `0xA4` supported-mode table as
  "device default modes" after the monitor EDID modes, including JSON output.
- `trigger5-tool test --list-modes` prints the monitor and adapter mode lists;
  `--device-mode N|WxH[@R]` forces an adapter-table entry using monitor, CEA or
  CVT timings.
- Driver: read the `0xA4` table once at probe and expose it read-only as
  `default_modes` on the card device's sysfs node.
- `VERSION` file and `scripts/bump-version.sh` to update the driver, DKMS,
  Debian and documentation version strings in one step.

### Changed

- Removed `linux-trigger5/trigger5-local.patch`; the tree is maintained
  directly and the local changes are documented in the READMEs.
- Non-EDID modes matching the adapter's `0xA4` table now pass the table index
  as the `SET_MODE` `wValue`, mirroring the Windows table path. EDID modes keep
  the custom path.
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
- Reset0 is retried while the adapter settles after a mode change, avoiding
  spurious `firmware reset failed: -121` errors.
- The moving-block label no longer draws outside its damage rectangle, which
  had left a one-pixel trail from the first character.
- The `default_modes` sysfs attribute uses `sysfs_emit_at()` for subsequent
  lines, avoiding an `invalid sysfs_emit` warning.
- 1024×768 displays through `trigger5-tool --device-mode 3`; KDE/xrandr
  validation followed in 1.0.7.

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
- Reverse-engineering export of the vendor Windows driver in a separate
  private repository (not shipped), including function maps, reconstructed
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

- 1920×1080 and 1024×768 are validated desktop modes. 1024×768 was verified
  through both `trigger5-tool --device-mode 3` and KDE/xrandr as of 1.0.7. The
  earlier blank-panel report was the adapter reset/settling path, not
  compression.
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

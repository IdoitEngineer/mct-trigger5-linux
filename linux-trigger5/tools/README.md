# Trigger 5 userspace tool

`trigger5-tool` probes the MCT Trigger 5 adapter and drives its DRM/KMS
display and hardware-cursor paths without X, a compositor or the vendor
driver in the way. It is a development and field-diagnostics helper, not part
of the kernel module or the DKMS package.

## Build

Dependencies (Debian/Devuan/Ubuntu):

```sh
sudo apt install build-essential pkg-config libdrm-dev libusb-1.0-0-dev
make -C linux-trigger5/tools
```

The result is `linux-trigger5/tools/trigger5-tool`.

## Commands

### `probe`

```sh
trigger5-tool probe          # human-readable report
trigger5-tool probe --json   # machine-readable report
```

Read-only and safe to run while X is active. It reports:

- **USB** via libusb: bus/address/port, speed, bcdUSB/bcdDevice, device class,
  string descriptors, every configuration, interface, alternate setting and
  endpoint.
- **sysfs**: the matching `/sys/bus/usb/devices/...` node.
- **module**: whether `trigger5` is loaded, refcount, version and every module
  parameter (`full_frame_always`, `full_frame_period_ms`).
- **DRM/KMS**: driver version and capabilities, connector status and modes,
  EDID parsed from `/sys/class/drm/*/edid` (manufacturer, product, serial,
  week/year, EDID revision, physical size, monitor name/serial), CRTCs, and
  every plane with its formats and properties (rotation, blend mode, damage
  clips, zpos, ...).
- **DRM sysfs**: `status`, `enabled` and `dpms` for each card output.

The tool never issues vendor control requests (`0xA1`, `0xA4`, `0xA7`,
`0xC8`). Those are owned by the kernel driver; the tool only uses the DRM
interfaces the driver exposes.

### `test`

Requires DRM master, so run it from a text console (`Ctrl+Alt+F2`) or stop the
display manager first:

```sh
sudo systemctl stop lightdm       # or sddm/gdm
sudo trigger5-tool test --landscape
```

The command:

1. finds the card whose DRM driver is `trigger5` (the card number can change
   between boots),
2. selects the connected connector and its preferred mode (or `--mode WxH`),
3. creates a dumb framebuffer and draws a test bitmap: colour bars, grid,
   orientation markers in the four corners, centre crosshair, title block and
   a live frame counter,
4. commits the mode through atomic KMS with the requested plane rotation,
5. animates a labelled block and drives the cursor,
6. restores the previous CRTC and drops master on exit (including `Ctrl-C`).

Rotation options are deliberately separate:

```sh
sudo trigger5-tool test --landscape       # DRM rotate-0
sudo trigger5-tool test --portrait-left   # DRM rotate-90  (90° counter-clockwise)
sudo trigger5-tool test --portrait-right  # DRM rotate-270 (90° clockwise)
sudo trigger5-tool test --upside-down     # DRM rotate-180
```

For portrait, the framebuffer is 1080×1920 while the CRTC keeps the panel's
1920×1080 mode; the driver rotates the pixels into the panel. The test pattern
is drawn in logical 1920×1080 orientation, so labels read correctly on a
physically rotated monitor.

Other options:

| Option | Meaning |
|---|---|
| `--mode WxH` | Use a specific connector mode instead of the preferred one. |
| `--seconds N` | Run for N seconds (default 10). |
| `--no-animate` | Show a static frame only. |
| `--full` | Send a full-frame damage rectangle each frame (slow, useful to compare against damage tracking). |
| `--damage` | Use per-frame damage rectangles (default). |
| `--no-cursor` | Do not create or move a cursor. |
| `--no-restore` | Leave the test mode active on exit (the next modeset or reboot recovers). |

### `cursor`

A shortcut for a landscape run focused on cursor movement (5 seconds by
default). It exercises the same `0xC8`/`0xE868` path through
`drmModeSetCursor2()` and `drmModeMoveCursor()`, including negative positions
at the top-left edge:

```sh
sudo trigger5-tool cursor --seconds 8
```

The Windows driver uploads the cursor as a 64×64 ARGB bitmap in a bulk
type-2 packet, followed by type-2 show/move/hide packets. The Linux driver now
implements that bulk path as well, in addition to the `0xC8` control writes;
hardware-verified at 1920×1080. With X's software rotation, X may or may not
use the cursor plane; see the main README for `full_frame_period_ms`.

## Troubleshooting

- `cannot become DRM master ... Permission denied` or `EBUSY`: something
  (X, a display manager, another tool) owns the card. Stop it or switch to a
  VT; `test` cannot run alongside X.
- `no Trigger 5 DRM card found`: load the module (`sudo modprobe trigger5`)
  and check `dmesg`.
- `driver exposes no cursor plane`: the loaded module predates the cursor
  plane; install the current DKMS package.
- If a run leaves the panel in a strange mode, restore it with
  `sudo trigger5-tool test --landscape --no-animate --seconds 1` or reboot.
- Watch the kernel side while testing:

  ```sh
  sudo dmesg -w | grep -E 'trigger5|usb 2-1'
  ```

  A `cursor enable failed`/`cursor position failed` warning means the control
  request did not complete; a `USB transfer failed ... adapter stopped`
  message means bulk I/O was latched off and the adapter needs a replug or
  `modprobe -r trigger5 && modprobe trigger5`.

# MCT Trigger 5 USB protocol

Protocol notes for the Magic Control Technology Trigger 5 USB 3.0 display
adapters (`0711:58xx`), as implemented by `linux-trigger5/trigger5.c`.

The protocol was reverse-engineered by Forest Crossman (cyrozap) from USB
captures and is documented here as observed. Field names marked `unknown` are
placeholders; values not listed are not sent by the Linux driver.

## Device and endpoints

| Property | Value |
|---|---|
| Vendor / product | `0x0711` / `0x5800`–`0x583F` (this unit: `0x5805`) |
| USB speed | SuperSpeed 5 Gbps |
| Interface | vendor specific, class `ff`, subclass `10`, protocol `00` |
| Bulk OUT | EP `0x01`, 1024-byte max packet, burst 12 |
| Interrupt IN | EP `0x84`, 8 bytes (not used by this driver) |
| Power | self-powered, 8 mA from the bus |

All control requests use `bmRequestType` `0x40` (host→device, vendor, device)
or `0xC0` (device→host, vendor, device).

## Control requests

| Request | Code | Dir | wValue | wIndex | Length | Purpose |
|---|---|---|---|---|---|---|
| `KEEPALIVE` | `0x91` | IN | `0x0000` | `0x0000` | 1 | disarm keepalive at probe |
| `GET_DEVICE_INFO` | `0xA1` | IN | `0x0000` | `0x0000` | up to 512 | device/interface descriptor |
| `GET_MODES` | `0xA4` | IN | `0x0000` | `0x0000` | 420 | supported-mode table |
| `GET_REGISTER` | `0xA5` | IN | `0x0000` | `0xEC34` | 4 | read register block |
| `GET_STATUS` | `0xA6` | IN | `0x00FF` | `0x0003` | 2 | monitor status; `byte[1] == 1` means connected |
| `GET_CAPS` | `0xA7` | IN | `0x0000` | `0x0000` | 4 | gated capability/feature word |
| `GET_EDID` | `0xA8` | IN | block index | `0x0000` | block size | read one EDID block from the monitor (DDC tunnel) |
| `SET_MODE` | `0xC3` | OUT | `0x0000` | `0x0000` | 35 | program timing + PLL (struct below) |
| `SET_REGISTER` | `0xC4` | OUT | `0x0000` | `0xEC34` | 4 | write register block |
| `SET_CURSOR_POSITION` | `0xC8` | OUT | X / `0` | Y / `0xE868` | 0 / 4 | move the hardware cursor, or write the cursor register |
| `FIRMWARE_RESET` | `0xD1` | IN | `0x0000` / `0x0201` | `0x0000` | 1 | re-initialise on-device firmware |

### Mode-set sequence

Recovered from captures; the driver issues it in `trigger5_crtc_atomic_enable()`:

```
FIRMWARE_RESET(0x0000)
SET_MODE(timing, pll)
FIRMWARE_RESET(0x0201)
GET_REGISTER(0xEC34)           -> 4 bytes
SET_REGISTER(0xEC34) = 60 00 00 10
```

Disable: **do not use `FIRMWARE_RESET(0x0001)`.** That value is not present in
the Windows driver; the adapter returns `-121`/`-110` and stops responding to
the following mode set. Stop the bulk stream and let the next enable sequence
(`0x0000`, `SET_MODE`, `0x0201`) program the new mode.

### `SET_MODE` payload (35 bytes, packed, big-endian)

| Offset | Field | Value |
|---|---|---|
| 0 | `height` | vdisplay |
| 2 | `width` | hdisplay |
| 4 | `line_total_pixels` | htotal − 1 |
| 6 | `line_sync_pulse` | hsync_end − hsync_start − 1 |
| 8 | `line_back_porch` | htotal − hsync_end − 1 |
| 10 | `unknown1` | `0xFFFF` |
| 12 | `unknown2` | `0xFFFF` |
| 14 | `width_minus_one` | hdisplay − 1 |
| 16 | `frame_total_lines` | vtotal − 1 |
| 18 | `frame_sync_pulse` | vsync_end − vsync_start − 1 |
| 20 | `frame_back_porch` | vtotal − vsync_end − 1 |
| 22 | `unknown3` | `0xFFFF` |
| 24 | `unknown4` | `0xFFFF` |
| 26 | `height_minus_one` | vdisplay − 1 |
| 28 | `pll.prediv` | PLL divider |
| 29 | `pll.mul1` | PLL multiplier |
| 30 | `pll.mul2` | PLL multiplier |
| 31 | `pll.div1` | PLL divider |
| 32 | `pll.div2` | PLL divider |
| 33 | `hsync_polarity` | 0 = positive, 1 = negative |
| 34 | `vsync_polarity` | 0 = positive, 1 = negative |

PLL model (reference clock 10 MHz):

```
pixel_clock = 10 MHz * mul1 * mul2 / (prediv * div1 * div2)
```

The driver searches that space for the closest match and rejects modes whose
error exceeds 10 000 ppm (1 %).

### Cursor

The visible Windows cursor is a **64×64 ARGB bitmap uploaded over bulk OUT**,
not a fixed hardware image. The `0xC8` control requests are a secondary path;
the bitmap/show/hide packets are what make the cursor appear. See the private
`CURSOR-BULK-PACKETS.md` notes (not shipped) for the captured packet format,
commands and the extracted bitmap.

Control path, used by both Windows and the Linux driver:

Enable, once after the mode set:

```
0xC8, OUT vendor device
wValue  0x0000
wIndex  0xE868
wLength 4
data    01 00 00 00
```

The adapter may STALL this request. Windows ignores the failure and continues;
the Linux driver latches the attempted state and sends positions anyway.

Position, for every cursor move:

```
0xC8, OUT vendor device
wValue  X
wIndex  Y
wLength 0
```

`X` and `Y` are signed 16-bit values. The Windows driver adjusts negative
coordinates by one (e.g. `x = -1` is sent as `0xFFFE`); the Linux driver does
the same.

Bulk cursor packets (counter type 2) carry the bitmap and the show/hide state:

| Command | Payload | Meaning |
|---|---|---|
| `0x0` | 16384 bytes | Upload a 64×64 ARGB bitmap (BGRA little-endian, alpha). |
| `0x3` | 0 | Show/move the cursor. |
| `0x5` | 0 | Hide the cursor. |

The type-2 counter is `0x2000 | sequence`. The command is the high nibble of
the 32-bit field at offset 12; the low 28 bits are the payload length.

### Device descriptor (0xA1)

Read-only blob; the Windows driver requests up to 512 bytes and parses the
first 10. Observed 14-byte reply:

```
02 01 05 00 08 04 10 01 01 12 01 0d 0a 1d
```

| Offset | Use |
|---|---|
| +0..7 | opaque 8-byte ID; byte 0 = interface type (2), byte 1 = subtype (1) |
| +3/+4 | version, `(byte3 << 8) | byte4` = `0x0008` |
| +8..9 | little-endian `0x1201` |
| +10..13 | unused by the Windows parser |

### Supported-mode table (0xA4)

Read-only table; request length is 420, the reply carries 4 + 8 × count bytes.

```
u16be count
u16   reserved
count × 8 bytes:
    u8    refresh_hz
    u8    timing_id
    u8    bpp        (24)
    u8    index
    u16le height
    u16le width
```

Observed for this adapter (19 entries):

```
index  mode        refresh
0      640x480     60
1      800x600     60
3      1024x768    60
4      1152x864    75
5      1280x720    60
6      1280x768    60
7      1280x800    60
8      1280x960    60
9      1280x1024   60
10     1360x768    60
12     1400x1050   60
13     1440x900    60
14     1600x1200   60
15     1680x1050   60
17     1920x1080   60
18     1920x1200   60
20     2048x1152   60
23     1366x768    60
27     1600x900    60
```

All are landscape; there is no portrait entry. `index` is a table ID, not a
sequence number.

The Linux driver reads this table once during probe, while the bulk stream is
stopped, and exposes the cached text form at
`/sys/class/drm/cardN/device/default_modes`. `trigger5-tool probe` prints it as
“device default modes”. It is informational: the monitor EDID remains the
authoritative mode source.

### Capability word (0xA7)

Read-only 4-byte little-endian value. The Windows driver queries it when the
device version from `0xA1` is `>= 0x3a`, or when the interface type/subtype is
not 2/0:

```c
if (version >= 0x3a || iface[0] != 2 || iface[1] != 0)
        caps = read_0xA7();
if (caps & 1) feature0 = 1;
if (caps & 2) feature1 = 1;
if (caps & 4) feature2 = 1;
if (caps & 8) feature3 = 1;
```

This adapter (version 8, interface 2/1) is queried and returns
`0x00000800`; none of the four feature bits are set. The `0x800` bit is not
used by this code path.

## Bulk frame protocol

Pixels travel on the bulk-OUT endpoint as a 20-byte header followed by raw
RGB888 data.

### Header (20 bytes, little-endian 16-bit fields)

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `magic` | `0xFB` |
| 1 | 1 | `length` | `0x14` (20) |
| 2 | 2 | `counter` | low 12 bits: frame counter; high 4 bits: flags (0 here) |
| 4 | 2 | `horizontal_offset` | low 13 bits: x of the update rectangle; high 3 bits unknown |
| 6 | 2 | `vertical_offset` | low 13 bits: y of the update rectangle |
| 8 | 2 | `width` | low 13 bits: rectangle width |
| 10 | 2 | `height` | low 13 bits: rectangle height |
| 12 | 4 | `payload_length` | low 28 bits: `width * height * 3`; high 4 bits flags |
| 16 | 1 | `flags` | `0x01` = uncompressed 24-bit RGB; Windows also uses `0x71` for its proprietary encoded path |
| 17 | 1 | `unknown1` | 0 |
| 18 | 1 | `unknown2` | 0 |
| 19 | 1 | `checksum` | two's complement of the byte sum of offsets 0–18 |

### Payload

- Format: RGB888 (24 bits per pixel), row-major, no padding, top-left origin.
- Size: `width * height * 3` bytes.
- The header is sent as its own bulk transfer; the pixel data follows in
  chunks of at most 128 KiB (`TRIGGER5_URB_BUF_SIZE`).
- Rows per chunk: `128 KiB / (width * 3)`. At 1920 pixels wide that is 22 rows
  per URB.
- Up to 8 URBs are kept in flight, round-robin. The driver waits for the
  oldest to complete before reusing it; a 2 s timeout latches `needs_reset`
  and stops I/O. It does **not** issue a USB port reset.
- A partial update's `horizontal_offset` and `width` must be 4-pixel aligned;
  1- and 2-pixel alignment corrupts the panel. The driver expands each damage
  rectangle outwards to that boundary before sending it.
- The driver may send several header-plus-payload sequences in one atomic
  commit, one per damage rectangle. Overlapping and vertically adjacent
  rectangles are coalesced first.

### Cursor packets (type 2)

Cursor traffic reuses the 20-byte header with packet type `2` in the high
nibble of the counter field (`0x2000 | sequence`) and a command in the high
nibble of the 32-bit field at offset 12:

| Offset | Field | Cursor meaning |
|---|---|---|
| 2 | counter | `0x2000 \| sequence` |
| 4, 6 | field A/B | cursor coordinates or surface offset (observed) |
| 8, 10 | width/height | `64`, `64` |
| 12 | command/payload length | high nibble `0` = bitmap upload, `3` = show/move, `5` = hide; low 28 bits payload length |
| 16 | flags | `0x01` |
| 17 | extra | shape/format identifier (`0x10` arrow upload, `0x31` I-beam upload, `0x00`/`0x21` show) |
| 20… | payload | 64×64 ARGB (16384 bytes) for command 0 |

Full examples and the extracted bitmap are in the private
`CURSOR-BULK-PACKETS.md` notes (not shipped).

### Frame buffer model

The header carries an **offset plus a rectangle**, which only makes sense if
the adapter keeps a complete frame in its own memory: each transfer updates a
region, and the display controller scans out the rest from RAM. Trigger 5
chips (e.g. T5-302) have 16 MB of frame memory; the exact size for `0x5805` is
not published.

Consequences:

- Partial updates are cheap and are what the driver does in steady state.
- A region that is never repainted stays on screen; after a modeset the driver
  therefore forces one full-frame update (local fix, see the driver README).
- The device does the scanout at the programmed mode rate (up to 1080p60),
  independent of USB throughput.

## Throughput and realistic frame rate

Per full 1920×1080 frame:

```
1920 * 1080 * 3 = 6 220 800 bytes  (~5.93 MiB)
60 fps          = 373 248 000 B/s  (~373 MB/s, ~2.99 Gbit/s payload)
```

USB 3.0 SuperSpeed is 5 Gbit/s on the wire with 8b/10b encoding, i.e. a
theoretical 500 MB/s payload. A 1080p60 full-frame stream therefore needs about
75 % of the theoretical link capacity.

Realistic expectations:

- **Link-level ceiling:** ~400–480 MB/s after link and protocol overhead,
  i.e. roughly **50–60 full frames/s** at 1080p.
- **Practical sustained full-frame rate:** estimated at ~45–55 fps once URB
  latency, the header/chunk overhead and the XRGB8888→RGB888 CPU conversion are
  included. A 60 Hz panel is therefore borderline but usable for full-screen
  motion.
- **Desktop use:** only damaged rectangles are sent, so typical bandwidth is a
  small fraction of that and updates feel smooth.
- **Mode size limit:** the driver rejects any mode whose RGB frame would exceed
  `TRIGGER5_MAX_FRAME_SIZE` (8 MiB). 1920×1080 fits (5.93 MiB); 2560×1440
  (11.1 MiB) and 4K do not.

To measure the real number on a machine, capture the bus with usbmon and
Wireshark (root required), or watch the xHCI interrupt rate while a full-screen
animation runs.

## Caveats

- Linux sends uncompressed 24-bit RGB (`flags=0x01`). The Windows Trigger 5
  driver contains a proprietary `EvoEncode` encoded-frame path (`flags=0x71`
  observed); the payload format is not decoded here. The Trigger 6 generation
  uses JPEG (libturbojpeg) and a different daemon, which is not compatible.
- EDID must be read while no bulk transfers are running; the adapter returns
  corrupt replies otherwise (`EDID block 0 … checksum is invalid`).
- Unknown header/request fields are sent as observed; changing them may break
  other Trigger 5 models.

## License

This document is part of the mct-trigger5-linux project and is licensed under
the GNU General Public License version 2 only (GPL-2.0-only). See the
[LICENSE](../LICENSE) file.

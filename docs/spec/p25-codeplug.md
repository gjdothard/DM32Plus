<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 codeplug integration

Status: the parameter codecs and shared vectors are imported from RadioFoundry
`c34dc440aaf2d3952458bc0bd85b8c56c76f6b23`. The generic keyed store, channel
tags, system readers and radio-core loaders are not connected yet. No P25
channel can be programmed or activated through this import alone.

## Reuse boundary

`core/p25/p25_codeplug.[ch]` preserves the upstream parameter functions:
conventional NAC/squelch, unit-ID inheritance, trunked system parameters,
site/control-channel bodies and identifier rows. Storage loaders are deferred
until their generic dependencies are ported. This avoids a separate P25 format.
The [provenance record](../../core/p25/UPSTREAM.md) identifies local adaptations.

The upstream integration uses these shared layers:

| Layer | Purpose | Local status |
|---|---|---|
| `codeplug_kv` | Versioned keyed EEPROM sector, CRC, journaled batches and raw-transfer preservation | Pending |
| `codeplug_tag` | Bind a channel kind and its parameters to a legacy channel | Pending |
| `codeplug_records` | Radio-wide records, systems, sites, control channels and identifier plans | Pending |
| `p25_codeplug` loaders | Resolve identity, targets, channel parameters and a trunked system | Pending |
| `radio_p25` | Start/stop engines, follow grants, retune and request permitted TX | Pending |

The tagged design stores a receive-only DMR stand-in in the legacy channel
record. The tag supplies the logical P25 kind and its TX permission. Older
firmware sees the stand-in and cannot transmit it. The companion CPS must map
its existing Receive only control to the logical permission and preserve the
safe base record. Its current legacy-only decoder is not sufficient.

## Parameter blocks

All multi-byte values are little endian. These are parameter bodies, not
independently validated stored records. The outer keyed store and live tag
must be validated before a mode uses them. A parameter default never grants TX.

| Body | Bytes | Fields |
|---|---:|---|
| Conventional | 5 | Squelch byte, RX NAC u16, TX NAC u16 |
| Radio unit ID | 4 | Unit ID u32, 1..0xFFFFFE; unset/out of range inherits the supplied radio ID |
| Trunked system | 10 | WACN u24, system ID u16, control NAC u16, options u8, hold seconds u8, roam dBm i8 |
| Site | 2 | RFSS u8, site u8; absent fields 0xFF |
| Control channel | 10 | Identifier/channel u16, RX Hz u32, TX Hz u32 |
| Identifier row | 18 | Base Hz u32, spacing Hz u32, signed TX offset i32, bandwidth Hz u32, type u8, slots u8 |

A NAC field's bit 15 marks an explicit 12-bit value. Unset or malformed fields
take upstream defaults. RX defaults to 0x293; TX defaults to RX, or 0x293 when
RX is any (0xF7E). RX 0xF7F and TX 0xF7E/0xF7F normalize to defaults.
Conventional squelch is normal (0) or selective (1).

Trunk options are skip encrypted (bit 0), programmed plan only (bit 1), and
no registration/affiliation (bit 2). The last option permits voice requests
without registration; it is not receive-only permission. System listen-only
and channel RX-only must independently clear every TX path in the owner.

## Checks and remaining gates

`p25CodeplugTest` uses all 39 upstream P25 rows, selected unchanged from the
shared channel-mode vectors. Conventional-only builds run the applicable 28.
The parser is adapted to standard C tokenization for native host portability.
Storage/tag loaders and their simulated-flash tests remain to be imported.
Complete the RX-only permission matrix, copy/delete/restart behavior, power-cut
checks and CPS preservation before activating P25 records.

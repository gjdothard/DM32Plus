<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 codeplug integration

Status: the parameter codecs and shared vectors are imported from RadioFoundry
`c34dc440aaf2d3952458bc0bd85b8c56c76f6b23`. The keyed store, channel tags,
system readers and P25 settings loaders are available through an explicit
configuration API. Stored conventional P25 now uses radio channel/PTT dispatch.
Trunked dispatch and mode-aware UI/CPS editing remain pending.

## Reuse boundary

`core/p25/p25_codeplug.[ch]` preserves the upstream parameter functions:
conventional NAC/squelch, unit-ID inheritance, trunked system parameters,
site/control-channel bodies and identifier rows. The full upstream storage
loaders now use the shared tag and system backend. This avoids a separate P25 format.
The [provenance record](../../core/p25/UPSTREAM.md) identifies local adaptations.

The upstream integration uses these shared layers:

| Layer | Purpose | Local status |
|---|---|---|
| `codeplug_kv` | Versioned keyed EEPROM sector, CRC and journaled batches | Imported; radio admission and legacy-transfer guards connected |
| `codeplug_tag` | Bind a channel kind and its parameters to a legacy channel | Imported; explicit configuration adapter tested |
| `codeplug_records` | Radio-wide records, systems, sites, control channels and identifier plans | Imported; exercised by stored P25 tests |
| `p25_codeplug` loaders | Resolve identity, targets, channel parameters and a trunked system | Restored; simulated-flash tests pass |
| `radio_p25` | Start/stop engines and request permitted TX | Conventional channel/PTT connected; trunked owner pending |

The tagged design stores a receive-only DMR stand-in in the legacy channel
record. The tag supplies the logical P25 kind and its TX permission. Older
firmware sees the stand-in and cannot transmit it. The companion CPS must map
its existing Receive only control to the logical permission and preserve the
safe base record. Its current legacy-only decoder is not sufficient.

`codeplug_channel` still returns the legacy base. Configuration code must use
`codeplug_tagged_channel`, `codeplug_tagged_set_channel` and
`codeplug_tagged_delete_channel` to resolve or edit tags. Applying logical TX
permission in an unaware radio reader would let its DMR path treat a tagged
channel as transmitting DMR. The radio uses `codeplug_radio_channel` and
recognizes tagged kinds before mode dispatch. The registry enables conventional
P25 with its build switch; other tagged channel kinds remain unrunnable.
See [tagged channel records](channel-modes.md) for the shared format.

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

### Stored transmit permissions

`p25_codeplug_tx_permissions` checks the stored channel against the active
channel's P25-relevant fields while holding the storage lock. It returns these
record permissions, separately from the tolerant receive configuration:

| Record condition | Voice | Voice request | Registration / affiliation |
|---|---|---|---|
| Conventional, TX allowed | Yes | No | No |
| Trunked, channel and system allow TX | Yes | Yes | Yes |
| Same, with `NO_REG` | Yes | Yes | No |
| Channel RX-only or system listen-only | No | No | No |
| Invalid, absent or stale required permission record | No | No | No |

A live binding and explicit TX flag are required. Conventional bodies must
be exactly five bytes with known squelch and valid encoded NACs. Trunked
channel bodies must be exactly one byte with known flags. Trunked system
bodies must be exactly ten bytes, with a five-byte identity declaration,
known sharing/options, an allowed system index, and the TX flag alone. A
secret-required or unknown system flag denies all permissions. Unknown tag
flags and longer future bodies also deny TX while remaining readable for RX.

The resolved unit ID must be 1..0xFFFFFE. A selected radio-wide unit record
must be exactly four bytes; zero inherits the existing radio identity. An
absent optional unit record can inherit, but a failed read cannot. Explicit
group targets must be 1..0xFFFF; private targets must be 1..0xFFFFFE and are
conventional-only. All call uses the protocol's defined group. A missing
selected contact/list or an unreadable referenced contact denies TX. With no
contact or list selected, the defined default talkgroup 1 remains valid.
Conventional system references are not implemented and deny TX.

These bits are necessary record permissions, not RF authorization. The mode
owner must also establish board support, valid control/identifier data, the
current session/grant and actual RX/TX frequencies, and normal radio protections.
Settings changes must invalidate the session and cancel queued signaling/audio.
NAC/identity/target overrides and VFO operation need their own validated settings
path. No current radio owner calls this API; runtime PTT, automatic signaling,
queued-work cancellation and CPS checkbox enforcement remain integration gates.

### Host validation

`p25CodeplugTest` uses all 39 upstream P25 rows, selected unchanged from the
shared channel-mode vectors. Conventional-only builds run the applicable 28.
The parser is adapted to standard C tokenization for native host portability.
`p25StoredTest` exercises full upstream loaders against simulated NOR flash:
unit-ID inheritance, contacts, NACs, truncated/default blocks, system entries,
identifier plans, fallback and system listen-only. Local checks cover logical
RX-only edits, safe legacy reads, copy/delete/restart and stale-name bindings.
The imported `codeplugTagTest` adds generic tag/live-binding, unknown-kind,
full-area, rewrite, target/list and system-record coverage (241 checks).
`p25AdmissionTest` checks the record permission matrix, malformed and future
bodies, identity/target bounds, stale channel fields, CRC/version failures,
and a failed read at every read position of the tested permission paths.
Sampled power cuts cover RX-only save, rename, delete, plain replacement and
RX-only copy, followed by real journal replay. The committed old state can
survive a failed edit. A recovered RX-only or unbound state never grants P25
TX; a successfully completed edit has its new permissions. Sampling is not
an exhaustive cut at every flash byte.

Complete runtime permission enforcement and CPS preservation before activating
P25 records. Host storage checks do not establish absence of RF emissions.

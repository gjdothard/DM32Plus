<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Keyed codeplug settings

Status: the generic store and its simulated-flash tests are adapted from
RadioFoundry `c34dc440aaf2d3952458bc0bd85b8c56c76f6b23`. It is currently
always built for tagged-channel admission and legacy-transfer protection.
Protocol settings readers remain feature gated. A recognized damaged, newer
or unreadable store cannot authorize radio operation or legacy codeplug writes.
Unrelated bytes without RFKV magic remain an ordinary legacy codeplug.

## Format

The store occupies the EEPROM image's 4 KB sector at offset 0xE000. It does
not overlap the existing channel AES selectors or talkgroup extensions.
The board's flash map supplies the EEPROM base. Multi-byte values are LE.

| Header offset | Bytes | Content |
|---|---:|---|
| 0 | 4 | RFKV magic: 52 46 4B 56 |
| 4 | 1 | Version 1; version 0 is damaged, later versions are read-only |
| 5 | 1 | Header length, at least 16 |
| 6 | 2 | Data length, within this sector after the header |
| 8 | 4 | CRC-32 of data bytes |
| 12 | Header remainder | Preserved fields; zero when first formatted |

Each data entry is key u16, length u16, then its value. The first entry of a
duplicate key is authoritative. Readers validate the complete data CRC and
entry bounds before returning a value. Short buffers copy a prefix and return
the stored length. Cursor walks stop when the checked area changes.

`codeplug_kv_lookup` provides a strict lookup for permission checks: it returns
true after a complete successful search and reports length -1 for an absent
key. It returns false for an invalid area, bad arguments or any failed read,
including an entry or value read after CRC validation. An optional field may
inherit a default only after a successful search proves it absent. The older
`codeplug_kv_get` API retains its tolerant absent/error behavior for existing
readers. Both APIs take the storage lock and use the first duplicate key.

`codeplug_kv_generation` exposes the full 32-bit edit generation for a running
mode session. Compare it alongside `codeplug_serial` for base-record edits.
The existing 8-bit `codeplug_kv_serial` remains the inexpensive UI/cache change
hint; a session must not rely on it after an entire low-byte wrap.

| Key range | Upstream family reserved for integration |
|---|---|
| 0x0100..0x01FF | Systems |
| 0x0200..0x02FF | System entries: sites, control channels, identifier rows |
| 0x0300..0x03FF | System secrets |
| 0x0400..0x07FF | Channel tags |
| 0x0A00..0x0AFF | Radio-wide mode records |
| 0x1000..0x17FF | Targets |
| 0x1800..0x18FF | Target lists |
| 0x1900..0x1CFF | Traffic keys |
| 0x1D00..0x1DFF | Reserved zone records |
| 0x1E00..0x1EFF | Per-system P25 receive favorites ([format](p25-ui.md#persistent-favorites)) |

Defining a key family does not activate its protocol or encryption support.
The sector is finite; a batch that cannot fit is refused without writing.

## Writes and preservation

`codeplug_kv_write` applies a batch to the storage subsystem's existing shared
sector buffer, then performs one journaled rewrite. Failure before commit
leaves flash unchanged; an interrupted commit is recovered by the journal at
boot. No second permanent sector buffer is allocated. Set/remove operations
keep unknown keys and longer headers. A head-only update keeps a value's
unknown tail. Rewrites discard later duplicate keys. No-change batches do not
wear flash or bump the keyed-store serial.

The local adapter requires a ready, owned storage region and a valid mapped
range without integer wrap. No flash read callback means no area. Before
formatting an area without RFKV magic, every byte of the sector must be 00 or
FF. A blank-looking header does not permit overwriting foreign payload later
in the sector. Unknown nonblank contents are left alone. A damaged area with
our magic can be rebuilt on an explicit write, as in upstream. Later versions
are never rewritten by keyed edits.

`codeplug_kv_keep` recognizes a raw transfer that would lose a valid or newer
area. `codeplug_kv_commit` holds the recursive storage lock across both that
decision and the journaled write. Another keyed writer cannot insert data
between them and have it erased by an older raw image. Transfer callers still
need to use this wrapper and reload the codeplug after a completed transfer;
that wiring is pending. The wrapper's existence does not protect current CPS
or TFTP paths until those callers are adapted and tested.

## Checks

`codeplugKvTest` reuses upstream's full simulated-NOR suite: format validation,
unknown fields, duplicate keys, scalar widths, head updates, batches, capacity,
walk cost, serial changes, raw preservation and power cuts. Local cases check
foreign payload after an empty header, unknown magic, map overflow, missing
callbacks, the storage foreign-region guard and a writer at the first unlock
boundary of a raw commit. No hardware or real codeplug is used.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Tagged channel records

Status: the shared MIT RadioFoundry format is imported for P25 configuration.
The radio reader applies tags and dispatches conventional P25 when `CONFIG_P25`
is enabled, and trunked P25 with `CONFIG_P25_TRUNK`. Other tagged kinds remain
parked. The generic tag/store guard is
always built so disabled or unsupported kinds cannot enter FM/DMR operation.

## Channel tags

Tags occupy keys 0x0400..0x07FF in the [keyed store](codeplug-settings.md),
indexed by channel number minus one. Multi-byte integers are little endian.

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 1 | Kind: 1 Tier III, 2 P25 conventional, 3 P25 trunked, 4 NXDN |
| 1 | 1 | System number; 0xFF means none |
| 2 | 2 | Name check |
| 4 | 1 | Flags: TX allowed bit 0, independent base bit 1, fallback present bit 2 |
| 5 | 4, optional | Fallback channel u16 and its name check u16 |
| 5 or 9 | Remaining | Mode parameter body |

The name check is FNV-1a over the stored 16-byte, NUL-padded name, with the
32-bit result's halves XORed. A mismatched name leaves the tag stale. IDs 0
and 0xFF are not kinds. Recognizing a stored ID does not enable its protocol.

Without the independent-base flag, a tagged channel's legacy record is DMR
and receive-only. Logical RX-only is the inverse of the tag's TX flag. A
zeroed flag byte grants no TX permission. Independent bases preserve their
own mode, bandwidth and RX-only setting during tagged edits.

Explicit tagged writes follow a live binding across rename, preserve longer
parameter tails, and remove a binding when replacing its channel with a plain
one. Copying the legacy record alone grants no logical TX permission. The
copy operation must also copy its live tag. Deletion removes the tag first.
These operations use the recursive storage lock; separate base/tag writes
are not a single journal transaction. Power-cut qualification remains pending.

`codeplug_channel` returns the raw legacy record. `codeplug_radio_channel`
adds logical tags and parks stale bindings or damaged, newer or unreadable
recognized metadata. An absent store or unrelated non-RFKV bytes retain legacy
behavior. Raw channel writes reject nonzero kinds; tagged edits use the explicit
adapter. Legacy transfer writers refuse codeplug changes while tags exist and
cannot replace the managed metadata sector. Reads remain available.

## Shared records

| Family | Header and body |
|---|---|
| Radio-wide mode | Mode parameter body, indexed by kind |
| Target / target list | Kind u8, flags u8, name 16 bytes, then address / references |
| System | Type, flags, sharing, identity length (one byte each), own ID u32, fallback channel/check (u16 each), name 16 bytes, then type body |
| System entry | System, class, group, flags (one byte each), then class body |
| Traffic key | System u8, algorithm u8, key ID u16, flags u8, then key bytes |

A reference is record number plus one (u16; zero means none), followed by its
name check (u16). A deleted or renamed target does not resolve under the old
reference. System entry classes are channel (0), site (1) and plan (2).
Channel-entry flag bit 0 identifies a control channel; group 0xFF is system-wide.

System flag bit 0 means a secret is required; bit 1 permits registration,
affiliation and TX. A clear TX bit means listen-only. Generic readers preserve
opaque mode bodies and do not authorize RF operation or enable encryption.
[P25 settings](p25-codeplug.md) define the imported mode bodies and remaining
permission gates.

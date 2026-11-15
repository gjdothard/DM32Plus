<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Message journal

The append-only journal operates on a caller-owned range of at least two 4 KiB NOR erase sectors.
The caller serializes access with other flash users and supplies a 192-byte scratch buffer. Mount
reads the sector headers and never programs flash. The firmware does not mount or write this
journal until the storage job and migration are wired up.

Each sector contains a 16-byte header followed by 21 slots of 192 bytes. All multibyte fields
are little endian. The header contains the four bytes `RMG1`, a 32-bit generation, a CRC-32 of
those eight bytes, three unused bytes, and a commit byte. Generations start at 1. Generation
`g` occupies sector `(g - 1) % sector_count`. Mount selects the highest committed, valid
generation in its expected sector.

A slot begins with a 32-bit record ID, source ID, destination ID, time, one flags byte and
144 bytes of zero-padded text (up to 143 bytes plus a terminator). A CRC-32 covers those fields.
The remainder is reserved. The commit byte is at the end of the slot. Both commit bytes
start erased, and the writer programs each to zero only after its contents are written.
The record ID is `(generation - 1) * 21 + slot_index + 1`, with no wrap.

A partially programmed slot is skipped on the next append. During rollover the writer commits
the new sector header after its first record has committed. The oldest generation leaves the
live window only then. One physical sector is always outside that window. A write or erase
failure requires remount before retry.

The writer reclaims a committed, expired sector automatically. An unknown, nonblank sector needs
explicit reclamation permission. That permission also covers a rollover target whose header was
lost in an interrupted erase or write. Migration must establish that the selected range can be
reclaimed, including any bytes left by older allocations. For 512 KiB the engine retains at most
2,667 slots once all 127 live sectors are full, before any future status or deletion records.

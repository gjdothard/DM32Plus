<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Message journal

The append-only journal operates on a caller-owned range of at least two 4 KiB NOR erase sectors.
The caller serializes access with other flash users. Reads need 192 bytes of scratch. Appends
reuse the shared 4 KiB sector buffer. Mount reads the sector headers and never programs flash.

The housekeeping adapter appends through the existing storage job slot. It refuses an
unavailable or overlapping board reserve and borrows the existing sector buffer for scratch.
If another caller holds the buffer, the job reports busy without touching flash. Requests
belong to the single job producer until the completion callback.

The Settings → Advanced action appears only when the feature is built and the reserve is
available. The confirmation defaults to Cancel. Enable queues activation and the UI reports
the result when the storage task finishes.

When the feature is built and a reserve is present, startup activates a blank reserve automatically. The Settings action
can retry activation. Either path takes the first sector of the reserve for an ownership marker. The
activation job checks that the range is erased and programs `RFMSG1 3C A5 00` (the six ASCII
bytes followed by three hexadecimal bytes) in order, with the zero commit byte last. A retry
can complete a recognizable partial marker
if the rest of the reserve is still erased. Activation never erases flash. An absent or
invalid marker disables append even if archived records are still present.

After activation, accepted received SMS and terminal outgoing results enter a two-record
queue on the housekeeping stack. The radio task copies the metadata and text without doing
flash I/O. Housekeeping appends in arrival order and retries when the shared buffer is busy.
Overflow and failed writes increment `data.history_dropped` in the `dmr_ext` diagnostics.
Messages still queued when power is cut can be lost.

The low bits of the flags byte are group (bit 0), outgoing (bit 1), send failed (bit 2),
and delivery acknowledged (bit 3). Outgoing without either result flag was sent without
confirmation. Text is stored as plaintext. The timestamp is zero until a trusted wall
clock is available. Record IDs preserve order across reboots.

The housekeeping task also provides read-only pages from newest to oldest. A request
returns up to four records into a caller-supplied buffer and inspects at most 32 slots.
A page can be empty and still have a continuation cursor. The cursor is an exclusive
record ID, so new arrivals do not move the next page. When the live window rolls past
a cursor, the read ends. Slots with invalid records are skipped. A flash read error
fails the page without advancing the cursor.

A private conversation matches the sender of a received message or the destination
of an outgoing message. A group conversation matches the destination talkgroup in
both directions. Private and group conversations can use the same number and still
remain separate. Reads share the storage job and sector buffer with other flash work.

The conversation directory shows the six newest distinct keys. It can display partial results
while scanning in bounded batches on housekeeping. The message view shows one record at a time,
with an older cursor and a way back to the newest record. A new browser request supersedes an
in-flight scan before the previous result can be displayed. The browser state is on the
housekeeping stack, and the UI shares the existing live-inbox title and body buffers.

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

A partially programmed slot is skipped on the next append. During rollover, the existing
[storage sector journal](settings-store.md) installs a complete replacement sector. It carries
the first record and a header whose commit byte is left erased. The writer commits that header
only after the sector replacement succeeds. The oldest generation leaves the live window only
then. One physical sector is always outside that window.

After a power cut, the storage layer replays a committed sector replacement before messages are
mounted. If the new header was still uncommitted, the next append validates the prepared first
record's ID and CRC, finishes the header commit, and writes the new message in slot two.

Rollover can start on an erased sector or on a committed, expired sector. A nonblank target
with unknown contents is refused. With a 512 KiB reserve, one sector holds the activation
marker and one is outside the live window. The remaining 126 sectors hold up to 2,646 slots
before any future status or deletion records.

## Interrupted-write testing

The host NOR fixture cuts writes before and after the storage journal commit. It also cuts
power again during reboot replay and simulates a target bit error and a temporary read failure.
Replay keeps the transaction pending until it can verify the copy.

For a physical power-cut test, use a lab radio and a build with its own empty test reserve.
Give it one marker sector and three journal sectors. Verify that the test reserve and storage
transaction sectors do not overlap live conversations, the codeplug, or factory data. A
generic loader write-test range is insufficient evidence of ownership.

Fill two journal generations with synthetic texts. Make one rollover attempt per checkpoint:
before the transaction header is committed, during target erase or programming, and after the
replacement is installed before the message header commit. After each reboot, verify the old
live records, the transaction completion byte, and the new record checksum. Repeat the test
with a second cut during boot replay. Keep the verified physical test results separate from
the host fixture results.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Firmware UI overhaul checkpoint

The hardware-tested baseline is commit `e14a655`, also released as `v26.11b6`. The overhaul lives on
`codex/firmware-overhaul`. Preserve radio behavior, channel and talkgroup data, and the existing update
path while consolidating the radio UI and aligning it with the CPS.

## Progress

| Slice | Status | Verification |
|---|---|---|
| Shared card header for Home, Quick Controls, Main Menu, and directory screens | Complete | Ten screenshots match the baseline byte for byte. Color and monochrome UI flows, the target build, and the static task-stack check passed |
| Shared list and selection behavior | Complete | All ten reference renders match. Color and monochrome UI flows, the target build, and the static task-stack check passed |
| Stable message selection | Complete | Color and monochrome UI flows, three DMR service configurations, the target build, and the static task-stack check passed. Ten reference renders match |
| Conversation journal engine | Complete | Host tests exercise rollover, power cuts, unknown sectors, and corrupt records |
| Deferred journal append | Implemented | Host tests cover a busy job slot, a busy buffer, and overlapping allocations |
| Persistent journal activation | Implemented | Host tests verify blank-only activation, interrupted marker writes, reboot, and refusal after marker corruption |
| Interrupted rollover recovery | Implemented | Host tests cut power during sector staging, target erase and program, and the final message commit |
| On-radio history activation | Implemented | Settings confirmation defaults to Cancel and reports the deferred result |
| Persistent SMS capture | Implemented | Host tests cover received and outgoing metadata, duplicate suppression, buffer contention, and reading committed records after reboot |
| Paged archive reader | Implemented | Host tests cover private and talkgroup filters, sparse pages, new arrivals, rollover, and flash read failures |
| On-radio conversations | Implemented | Simulator flows cover the directory, private and talkgroup views, and reading older messages |
| Conversation storage and Messages redesign | Pending | See the flash audit below |
| CPS and codeplug changes | Pending | Inventory existing records and test compatibility before adding persisted data such as Favorites |

The first slice is in [the screen renderer](../../core/ui/ui_screens.c). It factors out status and title
band drawing while keeping each screen's spacing and controls. The ten comparison frames cover Memory,
Dual Watch, VFO, DMR and FM Quick Controls, Main Menu, Zones, Channels, Talkgroups, and Contacts.

## Resume

1. Check out `codex/firmware-overhaul` and run `git status --short --ignore-submodules=all`. Retain any
   work in progress. Use `git log --oneline e14a655..HEAD` to find committed slices.
2. Review [the UI architecture](../spec/ui.md), the active slice above, and the current diff.
3. Record completed work, checks, and the next concrete task here before stopping. Keep observed
   hardware results separate from simulator and build results.

The list slice shares row backgrounds and footer drawing between Main Menu and the card lists.
List pages use the same item initialization for initial focus, scrolling, and numbered selection.
Keep the existing screenshots as the baseline for refactors and document intended visual changes
when a screen design changes.

## Operating talkgroup pool checkpoint

The installed baseline is v26.11b16 (`a8dcba73`). The connected-radio CPS test found the cause of the overflow
failure. The firmware advertised the feature, and the two independent reads agreed, but the existing overflow
sector had programmed bytes and lacked its recognized header. The CPS encoder correctly refused before sending.
A four-byte write to an erased pool slot passed a live CPS write and readback. The original talkgroup region
was restored and verified byte for byte. Six assignments through the current editor remain unverified because
its encoder still refuses the unknown sector.

Use two separately committed 16 KiB banks at codeplug image offsets `0x34000` and `0x38000`. Both banks were
read from the connected radio and were fully erased. The draft format holds 7,159 two-byte references per bank,
with a maximum of 63 per channel. Checksums cover the generation and payload. A torn inactive bank leaves
the previous generation readable. The offline converter creates saved talkgroup records for missing ID/slot pairs. The new bank stores
only saved slots and has no references into the legacy bytes.
Never claim an unrecognized bank containing data.

The bank codec, one-time converter, and firmware indexed reader have host coverage. The knob and picker
use indexed reads. The normal CPS write does not yet use the new bank. Next
implement the radio's recoverable bank writer and CPS transfer and migration, then add assignment controls and
the directory Add, Edit, and More screens. Verify both transfer directions and interrupted writes before building
and flashing. Contact Position behavior is a separate follow-up. Hardware RTC remains a separate feature branch.

## Next slice: conversation storage

The [DMR data service](../../core/dmr/dmrDataService.h) gives each record in its four-entry RAM inbox
a session-local token. The [UI adapter](../../core/ui/ui_svc_data.c) resolves the token for the
banner and open message. Private replies target the sender, and group replies target the destination
talkgroup.

The target build with capture reports 8 bytes of headroom in its primary RAM region. The
capture queue is on the housekeeping stack. Reuse existing storage-path buffers and account
for stack and RAM costs before adding conversation browsing.

The CPS and firmware now enforce a smaller DMR ID database boundary, leaving a 512 KiB reserve.
Treat that reserve as previously used until the journal migration, and account for older firmware
that still allows CPS writes there.

The [message journal engine](../spec/message-journal.md) mounts read-only and keeps a spare erase
sector for rollover. The deferred append job borrows the shared sector buffer. It requires
an activation marker written only into an erased reserve. The product build enables the journal
by default and initializes eligible blank storage at startup. Rollover uses the shared sector journal to recover after
power loss and continues to refuse unknown contents. The activation control lives under
Settings → Advanced → Message history on boards with a usable reserve and the feature enabled.
The message service captures accepted SMS and outgoing send results while activation is valid.
The archive reader drives a conversation directory and message view under Messages. It reads
the six most recent distinct conversations and pages through each selected history. Next, test
activation, message capture, and browsing on hardware using the verification gates below.

### Hardware gates

1. Before testing the journal, take and verify a full SPI backup on the owner's radio. Read the
   reserved range from that backup and record its state. With the boundary build, verify normal
   channel and caller-name behavior and confirm CPS reports the new size limit.
2. Once migration and the storage job are available, check that an unknown reserve stays intact
   until it is explicitly claimed. Write a few private and group messages, reboot, and verify
   their order and destinations. Test interrupted writes and first rollover with a lab-sized
   reserve before filling the complete reserve.
3. Verify the recovery path from a backup and the effect of rolling back to firmware with the
   older CPS boundary. Keep hardware observations in the bring-up record.

Baseline backup verified on November 15, 2026. Its 512 KiB reserve is all erased. The 20 KiB
service gap contains programmed bytes and is excluded. The installed 315,996-record database
fits the lower boundary.

Local hardware test build `v26.11b7` at `0f6f4876` installed on November 15, 2026 with the
journal enabled. The application-only updater verified all 150 written sectors by CRC.
After reboot, the radio reported the expected build and existing codec, the database
reported 315,996 records, and the journal diagnostic reported zero drops. The owner reported
that message history works on the radio. A normal reboot retained all three sampled archive
records byte for byte, and their CRCs passed before and after reboot.

The owner reported a private-call freeze with the TX indication stuck while the cable was
connected. The diagnostic link answered and reported no crash record or watchdog reset. The
owner repeated the call successfully with the cable unplugged. The cause is undetermined.
The radio stays on the test build. Interrupted-write and rollback checks are pending.

Host fault tests now cover a second cut during sector replay, a temporary read failure,
and a silent target write error. A physical cut needs an isolated reserve and fixture.

Local test build `v26.11b8` at `5b442dfd` is now installed. The updater verified all 150
written application sectors by CRC. After boot, the codec and database count matched the
baseline, and the sampled archive still held the same three records byte for byte. The
owner chose to move on from live journal testing. Physical fault testing remains a lab task.

## Scan overview

The card menu shows the live scan state, selected zone or VFO source, and configured scan
preferences. Scan membership remains zone-based and channel skip flags are edited in CPS.
The original scan actions remain available.

For test commands and screenshots, see [Testing](testing.md) and the [UI specification](../spec/ui.md).

## Follow-up: hardware RTC

Revisit hardware RTC support in a separate feature branch. Add an optional wall-clock interface and a bounded
board driver. Verify the RTC validity flag and retention on the radio across power cycles before trusting it.
Use valid GPS UTC to initialize or correct the RTC. Keep saved timestamps in UTC and apply the user's offset
only when displaying the clock or a message.

## Conversation inbox

Messages opens directly to the saved conversation directory. Private threads use the other radio ID regardless of
who started the exchange. Talkgroup threads use the group ID. Opening a thread starts at the newest message.
The directory shows the six most recent peers with the latest preview and time for each.
When history is unavailable, the live receive buffer is grouped by peer instead.

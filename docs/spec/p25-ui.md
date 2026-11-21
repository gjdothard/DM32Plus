<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 monitoring UI

`CONFIG_P25_UI` follows `CONFIG_P25`. It enables the P25 Home screen,
listening controls, recent talkgroups and system details. It can be disabled
without disabling P25 reception or transmit permission checks.

## Ownership and storage

The radio task publishes a bounded `p25_monitor_t` using a sequence counter.
Readers never dereference the decoder's borrowed workspace. The mutable
monitor lives inside that workspace. There is one permanent published copy.
Closing the owner clears the published view. The UI also verifies the
selected channel number and logical mode before using it.

The monitor carries the owner's base and keyed-store generations. Site-name
resolution rejects snapshots from an earlier codeplug or another selected
channel. It reads names through the radio service from stored site entries,
without dereferencing the decoder loan or caching a permanent name table.
The serving control frequency and decoded RFSS/site remain the name binding
during a voice grant. Pending control retunes publish hunting state without
the previous site's IDs. Closing or reloading the owner clears its view.

Recent history contains four distinct group destinations, newest first.
Private calls are distinguished by source. Entries carry a source ID when
decoded, actual tuned frequency, flags and last activity time. A grant with
an unknown source does not erase a source already learned for that group.
The history resets when the owner closes, including channel changes and
codeplug reloads. Programmed group contacts supply names; unknown IDs remain
numeric. Names are never inferred from the control channel.

## Receive controls

Requests are queued to the radio task with the selected channel number.
The task rejects requests for another channel, during transmit, or without
a current trunked owner. The follower serializes selection changes with
its provider callback. Changes apply to subsequent grants; a voice call
already playing finishes normally.

| Selection | Receive behavior |
|---|---|
| Programmed groups | Original channel filter and configured group hold time |
| All talkgroups | Every supported group destination, independent of the programmed transmit group |
| Favorites only | Only saved favorite group destinations; private calls excluded |
| Hold | Only the selected group, until another listening selection or owner restart |

The override hook uses -1 for the programmed filter, 0 for refusal and 1 for
acceptance. It does not modify the configured transmit group, engine TX
link control, registration, affiliation, RX-only bits or permissions.
Receive-only channel and system checks remain authoritative. Choosing all
does not add Phase 2 or encrypted voice support.
The monitor publishes the programmed transmit target separately. When TX is
configured, Home names that target; while transmitting, the call card shows
the transmit destination rather than the last received group. A receive Hold
never silently changes the transmit destination.

## Persistent favorites

Four favorites are stored per trunked system under key `0x1E00 + system`.
The value has 17 bytes: version 1, count, system name check (little-endian
u16), five declared system identity bytes, then four little-endian u16 group
IDs. Zero and duplicate IDs are invalid. Unknown versions, invalid lengths
and replaced identities read as an empty list. Storage read failures and
unknown versions or lengths refuse edits. Writes use the keyed-store journal
and preserve unrelated keys.

The UI posts edits to the housekeeping storage job. The job checks the
captured generation and channel binding before writing. A changed keyed
store causes the normal owner reload, clearing temporary holds and recent
history. Favorites remain available after that reload and across restarts.
Favorites-only selection requires a nonempty list. Saving a favorite resets
the receive selection to the channel's programmed behavior, as other
codeplug edits do; that programmed behavior may follow all groups.

## Presentation and diagnostics

Tall color screens use the same card status bar, surfaces, fonts and footer
helpers as the existing Home and menus. The P25 call card separates the group
ID, name and source; recent entries use two rows. A host pixel check verifies
that P25 Home actually uses the card surface. Small and monochrome displays
retain the standard layout. Context opens Recent; Quick opens listening.

Trunked card Home centers reception and signal icons between P25 TRUNK and
RX only. Linked squares mean acquired control, a speaker means a voice grant
or reception, and a search symbol means acquisition. These are separate from
the header's keypad-lock and privacy indicators. TX and denied TX remain
explicit. Exact signal strength remains in System details.

A site line replaces the separate state/dBm row. It shows the confirmed site
name, numeric RFSS/site fallback, an explicitly labeled Candidate during
search, or Site mismatch. System details retain the full name and decoded
identity. Small and monochrome layouts use a text site line. Conventional
Home retains its existing presentation.

Home replaces FM-specific controls with P25 reception state, current or last
talkgroup, programmed name, source, tuned/control frequencies, listening
selection and signal strength as space permits. The footer retains RX-only
visibility on smaller displays. Conventional channels omit trunk controls.
Private calls are labeled separately. A remembered refusal is labeled
**Last skip** in system details, rather than presented as a current fault.

System details show decoded WACN, system ID, RFSS, site and NAC, valid blocks,
CRC failures and the last skip reason. Unknown identities use `--`.
IDLE/control acquisition is not evidence of decoded voice or audible audio.

The debug link appends section `p25` without renumbering existing sections.
It exposes `p25.ui`, owner/channel/flags, receive selection/held group,
decoded identity, frequencies, block/CRC counts, history/favorite counts
and the newest talkgroup/source/time. The section reports `p25.ui=0` when
the feature is disabled.

With trunking enabled, `p25.site_name` is at most 16 Latin-1 characters.
`p25.site_state` is 0 unknown, 1 candidate, 2 confirmed, 3 mismatch or
4 unavailable/stale. Unknown, mismatched and stale names are empty.

## Integration checklist

The Home encoder steps configured P25 groups using a signed detent count. The
radio task resolves each step against its current cached target, so queued
steps cannot overwrite one another using a stale UI index. Selection wraps in
both directions and uses the assigned-group selection command, changing receive
hold and transmit target together. PTT, active TX and stale channel bindings
refuse selection. Existing Receive Only and system permissions remain in force.
The current count/index reuse the shared published radio view; the UI does not
read the decoder loan. The selected idle group and name use the shared Home
card and standard layouts. No configured groups means ordinary channel stepping.

The listening menu also opens the existing operating-talkgroup picker. P25
uses the shared channel assignments, names and ordering; timeslot labels are
omitted. The channel name appears above the assignments, separately from the
page title. Recent and favorite selections use inverse text on monochrome
displays. System status uses a shorter translated value when the full status
would crowd its label. Selecting an assignment holds that group and changes
the transmit target. Recent-call Hold and favorites remain receive-only operations. The
selection does not grant TX permission. See [codeplug integration](p25-codeplug.md).

- [x] Bounded published monitor and receive selection independent of TX.
- [x] Home, listening menu, recent/favorite list and system details.
- [x] Journaled per-system favorites and translated text.
- [x] Host receive-selection, persistence and screen-opening regressions.
- [x] Complete target stack/configuration checks and gallery review.
- [x] Installed target: read-only navigation, control decode and RX-only checks.
- [ ] Owner field test of the new screens, Hold and favorites.

See [P25 trunking](p25-trunking.md), [keyed settings](codeplug-settings.md),
[the UI contract](ui.md) and [the user instructions](../user/p25.md).

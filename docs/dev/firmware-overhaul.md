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
| Shared list and selection behavior | Pending | Exercise keypad filtering, focus, Back, and PTT targets |
| Remaining screen designs | Pending | Start with Messages and identify which conversation behaviors the current message store supports |
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

The next slice starts with the card lists and Main Menu footer. Review the focus and row helpers before
changing navigation. Keep the existing screenshots as the baseline for refactors and document intended
visual changes when a screen design changes.

For test commands and screenshots, see [Testing](testing.md) and the [UI specification](../spec/ui.md).

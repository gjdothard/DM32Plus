<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Firmware UI integration record

The firmware UI overhaul, conversation history, saved contact editing, shared operating talkgroups,
hardware clock support, and CPS improvements were integrated into `main` for the 1.0.0 release series.
The former feature-branch resume instructions and intermediate implementation plans are obsolete.

## Current references

| Area | Implementation and behavior |
|---|---|
| Shared card frames, menus, field editors, and pickers | [UI specification](../spec/ui.md), [design audit](../ux/design-consistency-audit.md) |
| Conversation archive and interrupted-write recovery | [Message journal](../spec/message-journal.md), [Messages](../user/10-messages-and-signaling.md) |
| Saved contacts and recoverable shared operating talkgroups | [DMR voice](../spec/dmr-voice.md), [codeplug editing](../spec/codeplug-native.md), [CPS](codeplug-editor.md) |
| GPS-synchronized retained UTC | [Radio time](../spec/radio-features.md#56-time-of-day) |
| Current screens and interaction flows | [Gallery](../ux/screens/), [navigation](../user/05-keys-and-navigation.md), [host UI tests](testing.md#the-ui-simulator-and-the-gallery) |

## Verification limits

Host tests cover the journal engine, activation, rollover recovery, deferred writes, conversation browsing,
contact editing, shared-bank transfer, and UI flows. They do not establish hardware interoperability or
power-loss behavior on every board. Hardware observations belong on each radio's bring-up page, linked
from the [testing index](../testing/README.md).

Physical interrupted-write and rollback testing require a suitable isolated reserve and owner-authorized
hardware work. A normal reboot persistence check does not replace those tests. Use the current build's
size and stack reports; RAM figures from intermediate feature builds are not current budgets.

## Further work

The [design audit](../ux/design-consistency-audit.md) lists specialized layouts still outside the completed
card families. New persisted data requires a storage and compatibility design before implementation.
Keep behavior specs, user instructions, and screenshots aligned when changing the UI.

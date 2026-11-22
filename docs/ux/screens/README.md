<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Firmware screenshots

These are actual firmware UI renders with synthetic simulator data. The tall dark card layout below
covers Home, menus, conversations, editors, and utility pages.
The other display folders retain their own standard layouts. Feature availability is listed in the
[feature matrix](../../feature-matrix.md).

## Home and Quick Controls

<img src="240x320i10x5/home_dmr.png" width="220" alt="Single-channel card Home">
<img src="240x320i10x5/home_dual_two_rx.png" width="220" alt="Dual watch with calls on both channels">
<img src="240x320i10x5/quick_dmr.png" width="220" alt="DMR Quick Controls">
<img src="240x320i10x5/hub.png" width="220" alt="Nine-entry Main Menu">

## Calls, channels, and targets

<img src="240x320i10x5/caller_actions.png" width="220" alt="Numbered caller actions">
<img src="240x320i10x5/recent.png" width="220" alt="Activity and Logs caller cards">
<img src="240x320i10x5/channels.png" width="220" alt="All Channels with mode and frequency details">
<img src="240x320i10x5/find_name.png" width="220" alt="Find results with names and IDs">
<img src="240x320i10x5/talk_target_picker.png" width="220" alt="Talk target picker">
<img src="240x320i10x5/operating_talkgroups.png" width="220" alt="Saved operating talkgroup assignments">

## Messages and conversations

<img src="240x320i10x5/message_conversations.png" width="220" alt="Persistent conversation directory">
<img src="240x320i10x5/message_thread.png" width="220" alt="Full conversation bubbles and delivery result">
<img src="240x320i10x5/message_thread_older.png" width="220" alt="Browsing older messages">

## Dual watch and settings

<img src="240x320i10x5/dual_actions.png" width="220" alt="Dual Watch action cards">
<img src="240x320i10x5/dual_pick.png" width="220" alt="Channel picker for the selected pane">
<img src="240x320i10x5/settings_display.png" width="220" alt="Display setting cards">
<img src="240x320i10x5/settings_editor.png" width="220" alt="Numeric editing with its allowed range">
<img src="240x320i10x5/dmr_id.png" width="220" alt="My Radio identity fields">
<img src="240x320i10x5/language.png" width="220" alt="Language selection and active catalog">

## Sharing, confirmations, and tools

<img src="240x320i10x5/zone_actions.png" width="220" alt="Zone channel actions">
<img src="240x320i10x5/zone_share_to.png" width="220" alt="Sharing recipient selection">
<img src="240x320i10x5/factory_reset.png" width="220" alt="Factory reset with the safe initial choice">
<img src="240x320i10x5/usb_info.png" width="220" alt="USB network information">
<img src="240x320i10x5/ars.png" width="220" alt="ARS and LRRP status and controls">

## Reproducing the gallery

Run `make -C tests/host ui-gallery`. The simulator uses its real input handlers, including the
`branded-home` layout and `history` fixture, to reach each screen. Its gallery build includes message
history support; this does not change a radio build's feature configuration.
Contact sheets are in `sheets/`. Review changed frames and keep only intended image changes.
See [UI testing](../../dev/testing.md#the-ui-simulator-and-the-gallery) for individual renders.

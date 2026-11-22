<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Channels, calls, and scanning

Program channels in [CPS](04-programming.md), or open **Main Menu > Radio > Channels** on the radio.
Channel cards show the number, name, mode, and operating details. Digits filter the list; **OK** opens the editor and **#** tunes the focused channel.

## Zones and channels

**Main Menu > Zones / Channels** opens your zones. A zone contains an ordered selection of channels.
Use its actions to edit membership, rename it, or share it. Deleting a zone asks for confirmation.
Sharing uses DMR data; check the destination and channel before sending.

On Home, the knob changes channel. The Up/Down behavior can be reassigned in **Settings > Keys**.
Use the channel editor for saved RX/TX frequencies, power, FM tones, and DMR parameters.
Leaving an edited channel offers to save or discard changes.

## P25

P25 channels show **P25** on Home, including the card layout. This identifies
the selected channel mode; it does not confirm a control-channel lock.
Use CPS to edit P25 channel and system settings.

## FM

Use Quick Controls for squelch, power, bandwidth, and tones. Monitor opens squelch while held when that role is assigned.
CTCSS/DCS tones control squelch access; they do not make a call private.
During an FM transmission, number keys, **\***, and **#** send DTMF tones.

## DMR calls and contacts

A DMR channel needs matching frequency, color code, and timeslot. Group calls use a talkgroup; private calls use one radio's ID.
Set your own DMR ID in CPS or **Settings > My radio**.

Hold **#** on a DMR channel to choose a talkgroup or ID. The selected talk target can be temporary; **Back** on Home clears it.
**#** on Home opens actions for the current or last caller. **Main Menu > Activity / Logs** opens Recent calls;
select a call to inspect it, and use its actions or **PTT** to call back.

**Contacts & Talkgroups** keeps saved destinations and operating talkgroups. RX group lists add groups a channel receives.
Digital monitor broadens which DMR groups you hear. It does not change the channel's transmit destination.

Basic privacy and AES assignments must match the other radios. Configure persistent AES slots through
[CPS Encryption](04-programming.md#encryption-slots). Do not assume a privacy label proves interoperability with another firmware.

## VFO

Select the VFO from the zone/channel controls, or use **Copy to VFO** from the current channel's actions.
Type a frequency and press **OK** or **#** to tune. Up/Down and the knob step by the configured VFO step.
Quick Controls provides the mode-specific settings, including DMR color code and timeslot or the FM TX frequency for a split.
Selecting a memory channel leaves the VFO.

An FM channel or VFO frequency in the air band (108 to below 137 MHz) selects AM receive;
65 to below 108 MHz selects broadcast FM receive. These bands are receive only. The current build includes these paths,
but their hardware verification is not recorded; do not treat an enabled setting as proof of successful reception.

## Scan

Open **Main Menu > Scan controls**, then choose **Start scan** or **Stop scan**.
Scan pauses on activity. Use the available scan actions to skip a nuisance channel or change the scan state.
Review channel scan flags in CPS if expected channels are skipped.

## Dual watch

Use Quick Controls or the assigned **Talk target** key's hold action to switch dual watch on or off.
Home shows A and B and indicates which pane owns **PTT**. Use Dual Watch actions to choose B or change a pane's channel.
The other pane's channel is marked in use in the picker.

**Settings > Talk & listen > Dual watch talk back** allows a received call on the other pane to move the reply target
temporarily. Check the PTT indication before transmitting. **Dual watch delay** controls the wait after a call.
Dual watch is not a promise of two simultaneous independent receivers.

## Emergency

Holding a side key assigned to **Power level** for 1.5 seconds starts emergency behavior.
Review the emergency destination and configuration before using this role. Use the screen's stop action to end emergency mode.
An emergency indication does not guarantee that another station received it.

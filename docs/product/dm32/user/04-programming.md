<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Program with CPS

Open **CPS** in the header. Editing files works locally; reading or writing the radio requires a browser with serial-port
support and the programming cable. Start the radio normally with DM32+ running.

## Read, edit, and write

1. Choose **Connect** and select the cable's port. The connection bar identifies the radio.
2. Choose **Read from Radio** before editing an existing configuration.
3. Save a file copy. Edit the appropriate tabs and review **Problems**.
4. Choose **Write to Radio**. Check the target radio and the changes shown before starting the write.
5. Wait for verification. Keep the radio powered and connected until it finishes.

A large DMR ID database can take a long time over the programming cable. If CPS offers to skip reading it, the later
codeplug write preserves the database already on the radio. Use the **DMR ID Database** tab to read or replace it separately.

If the first write offers **Prepare Radio**, read its explanation and keep the backup. Stock data may occupy the area
the firmware needs for its settings. Preparation is separate from ordinary codeplug editing.

## Editor tabs

| Tab | Use |
|---|---|
| Radio | DMR ID, radio name, boot text, and call hang times |
| Encryption | Persistent AES key slots and the radio default on a compatible connected radio |
| Channels | FM/DMR frequencies, power, tones, timeslot, color code, contacts, and channel options |
| Zones | Channel collections and their order |
| Contacts | Saved group and private-call destinations |
| RX Group Lists | Additional talkgroups a DMR channel receives |
| DTMF Contacts | Stored tone sequences |
| APRS | Codeplug APRS configurations |
| DMR ID Database | Caller names associated with DMR IDs |
| Problems | Errors that block writing and warnings that need review |

The **Convert Operating Talkgroups** action derives shared operating talkgroups from channel contacts and receive groups.
Review the conversion before accepting it. Shared talkgroups need firmware and editors that preserve this extension.

## Files and drafts

**New** starts a codeplug. **Open File** identifies supported images and imports. **Save** offers the supported file formats,
including the codeplug image, CSV, and qdmr YAML. Review import warnings: formats can differ in capacity and supported fields.

CPS keeps a working draft in this browser. A restored-draft notice identifies it when you return. Download a file for a
durable backup; a browser draft can disappear when browser storage is cleared. Use **Undo** and **Redo** while editing.

## Encryption slots

AES key values are written through the live radio connection. They are excluded from codeplug exports, saved drafts,
and ordinary reports. Keep your own secure record of keys you need to restore. Selecting a slot does not recover its secret value.
Check the radio default and each channel's encryption assignment before writing.

## Connection ownership and privacy

Only one application can use the programming cable at a time. Disconnect CPS before opening the installer or a host program.
Codeplug conversion runs locally. RadioID lookups require internet access and send an API key to RadioID only when you use that service.
Error reports include a codeplug only when you choose to attach one.

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
| Channels | FM/DMR/P25 frequencies, power, mode parameters, contacts, talkgroup assignments, and permissions |
| Systems | P25 trunked identities, control channels, named sites, and channel plans |
| Zones | Channel collections and their order |
| Contacts | Saved group and private-call destinations with DMR, P25, or combined protocol selection |
| RX Group Lists | Additional talkgroups a DMR channel receives |
| DTMF Contacts | Stored tone sequences |
| APRS | Codeplug APRS configurations |
| DMR ID Database | Caller names associated with DMR IDs |
| Problems | Errors that block writing and warnings that need review |

The **Convert Operating Talkgroups** action derives shared operating talkgroups from channel contacts and receive groups.
Review the conversion before accepting it. Shared talkgroups need firmware and editors that preserve this extension.

## Contacts and channel talkgroups

The contact **Protocol** field selects DMR, P25, or DMR and P25. Channel pickers show compatible entries.
Names and IDs can differ between protocols, including contacts with the same numeric ID.
In a channel's **Talkgroups** tab, use **Add** or **Create and assign**, then reorder or remove assignments.
Editing a shared contact affects every channel using it; removing one assignment does not delete the contact.
DMR assignments may override timeslot. P25 uses decimal group IDs from 1 to 65535 and ignores timeslot overrides;
the first assignment is its initial TX target. Write the codeplug after editing.

## Files and drafts

**New** starts a codeplug. **Open File** identifies supported images and imports. **Save** offers the supported file formats,
including the codeplug image, CSV, and qdmr YAML. Review import warnings: formats can differ in capacity and supported fields.
Direct radio programming with qdmr is unsupported on this product; use file interchange or the
[migration reference](../../../user/19-moving-from-stock-dm32.md). P25 records require binary backups, as described below.

CPS keeps a working draft in this browser. A restored-draft notice identifies it when you return. Download a file for a
durable backup; a browser draft can disappear when browser storage is cleared. Use **Undo** and **Redo** while editing.

## Encryption slots

AES key values are written through the live radio connection. They are excluded from codeplug exports, saved drafts,
and ordinary reports. Keep your own secure record of keys you need to restore. Selecting a slot does not recover its secret value.
Check the radio default and each channel's encryption assignment before writing.

## Portable Windows CPS

When a release offers a portable offline CPS ZIP, extract the entire package and run **Start CPS.cmd**.
It starts the bundled site on loopback; no Python installation is needed. Use Chrome or Edge for serial programming.
Core editing and programming work offline with the included resources. Online ID downloads, changed-feature
firmware builds, and external links need internet access. Keep the same extracted package location and browser
profile for drafts, and download durable backups. Download channels and separate packaging are described in
[Install or update](02-installing.md#choose-firmware).

## Connection ownership and privacy

Only one application can use the programming cable at a time. Disconnect CPS before opening the installer or a host program.
Codeplug conversion runs locally. RadioID lookups require internet access and send an API key to RadioID only when you use that service.
Error reports include a codeplug only when you choose to attach one.

## P25 programming

The editor offers **P25** and **P25 Trunking** channel modes for Phase 1.
Conventional channels have RX/TX NAC and squelch fields. Use **Systems** to
enter trunked-system identity, control channels, optional sites and channel
plans. Select that system on each trunked channel. The current firmware
supports eight systems. Optional site names have up to 16 printable Latin-1 characters; match the broadcast
RFSS/site IDs and assign each control channel to its site group. Phase 2 voice is not supported.
See [P25 operation](08-p25.md) after programming.

**RX Only** remains the channel transmit permission. On a trunked channel it
also prevents registration, affiliation and service requests. New trunked
channels start RX only, and new systems start **Never (listen only)**. To
transmit, the channel and its system must both permit it, and all normal
frequency and transmit checks still apply. Listen-only systems never send
automatic signaling. The **Radio** tab has a P25 unit ID; blank inherits the
DMR ID. A system may override that ID.

Save P25 codeplugs as binary backups. Channel CSV and qdmr YAML cannot
represent the permission and system records; the editor refuses these
operations while those records are present. Other CSV tables remain usable.
Existing inline DMR talkgroup assignments are migrated to the shared pool
when P25 is first saved. Unknown occupied storage or insufficient capacity
is reported instead of overwritten. Keep the original backup.

Writing P25 requires firmware advertising managed staged programming in
fast-protocol INFO. Older firmware is refused before a write; there is no
legacy-write fallback. The radio stops active calls before the commit and
reloads the resulting permissions before resuming. Keep radio identity also
preserves the radio-wide P25 unit ID. Host tests check this path with synthetic flash and simulations. Consult the
[bring-up record](../../../testing/dm32-bringup.md) for recorded hardware checks;
a successful transfer does not establish live-system interoperability.

If CPS reports that managed P25 transfers are unavailable after an update,
disconnect and reconnect to refresh the capability check. The initial
1.1.0 RC builds omitted the required board staging reserve; those builds
need a corrected firmware image even though they display the P25 modes.
Save the edited codeplug as a binary file before updating the firmware.

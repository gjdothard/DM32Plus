<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Troubleshooting

## CPS or the installer cannot connect

1. Use a browser with Web Serial support, such as Chrome or Edge.
2. Check that the programming cable is fully inserted and its USB-to-serial adapter appears as a port on the computer.
3. Close stock CPS, terminals, PPP, hotspot hosts, and other browser tabs using that port.
4. Start the radio normally for CPS. Enter the bootloader only when the installer asks.
5. Reconnect and select the correct serial port.

Use the adapter manufacturer's driver when the cable has no working serial port.
Do not replace its driver with WinUSB/Zadig: native-USB DFU instructions apply to other hardware.

## Codeplug write is blocked or refused

Open **Problems** and resolve errors first. Review import warnings and the connected radio's identity.
If a radio just migrated from stock offers **Prepare Radio**, read the explanation and save the backup before proceeding.
Keep a downloaded codeplug copy before retrying. A successful firmware flash does not prove that a codeplug was migrated.

## DMR voice is silent

Check volume, frequency, color code, timeslot, talkgroup, and the receive group configuration.
Confirm the installer reported a usable AMBE codec. An image installed without the codec can still show DMR data and call activity.
Check that privacy type, key, and slot assignments match the other station.
If FM works and DMR voice does not, include the installer's codec result in your report.

## Messages do not arrive

Check both DMR IDs and channel parameters, then the outgoing message format under **Settings > Connections**.
Group messages have no delivery acknowledgment. A private message may arrive even when its acknowledgment is lost.
See [Messages](10-messages-and-signaling.md) for the meaning of each send result.

## Clock shows `--:--`

Allow GPS to acquire a fix and check the GPS setting. Set **Location > UTC offset** for local time.
Messages saved without a valid timestamp can continue to show an unknown time after the clock is established.

## Firmware will not start

Power off, remove and refit the battery, and try again. If necessary, enter update mode with **PTT + SK1** held at power-on.
Use the installer or the compatible stock updater with the correct image. Preserve your backups and calibration data.
See [Install or update](02-installing.md#if-something-goes-wrong).

## Reporting a problem

Open [this project's issues](https://github.com/gjdothard/DM32Plus/issues/new/choose). Include:

- The firmware version from **Settings > Advanced > About**.
- What you did, what you expected, and what happened.
- Whether it reproduces after a restart, and the browser/operating system for a web-tool problem.
- A screenshot or the crash-log details when relevant.

For a web-tool error, **Report a Problem** downloads a report you can attach to the issue.
Codeplugs contain identifiers and contacts; include one only when you intend to share it.
Encryption key values are not included in ordinary reports. Review attachments before posting.

The [source testing guide](../../../testing/README.md) contains contributor test procedures.
The published guide describes the current product workflow; historical bench notes are not installation instructions.

## Command-line installs (advanced)

Run a manual kit's commands from the extracted kit directory and use its included requirements and tools.
Check the serial-port name, close other applications using it, and retain the command's complete error output.
For migration-tool details, use the [source reference](../../../user/19-moving-from-stock-dm32.md).

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Install or update

Open **Install** in the header, or use **Firmware** in CPS. Use a Chromium browser with serial-port support,
such as Chrome or Edge, and the DM-32UV programming cable. Close stock CPS, terminal programs, and other tabs using the same port.

## Before installing

- Save your stock codeplug with its programming software and keep a copy of the manufacturer's firmware.
- Charge the battery. Keep power and the cable connected throughout a write.
- Keep every backup the installer downloads. A firmware image alone is not a backup of your channels and settings.

The installer checks the image and its manifest before writing. Use DM32+ release files intended for this radio.
The AMBE voice codec is not distributed in the release image. Follow the installer when it asks to preserve the
codec from the radio or extract it from a supported stock firmware file. File processing runs on your computer.
Without a usable codec, analog FM and DMR data can work, but DMR voice cannot.

## Choose firmware

| Choice | Meaning |
|---|---|
| Bundled | The firmware included with this copy of the site or portable CPS, when available |
| Stable | The latest stable release published by this project |
| Development | A development prerelease |
| Testing | A release candidate intended for testing |

Online channels need an internet connection. A channel with no matching published image is unavailable.
Semantic release tags use forms such as `v1.0.0`, `v1.0.0-dev.1`, and `v1.0.0-rc.1`.
Choose **Full** to use the published product image. To change what is included, choose a preset or **Custom** and
select the features before continuing. Dependencies follow your choices: turning off a feature also turns off its dependents.
Programming and recovery remain enabled so CPS can still connect and update the radio.

A changed selection is compiled on demand and requires internet access to the build service. CPS shows queue and build progress;
matching completed selections use the build cache. A build that exceeds memory limits reports the problem instead of being flashed.
The selected release channel still determines the firmware version.

## Install

1. Choose the install action and the firmware channel.
2. Select the programming cable's serial port when the browser asks.
3. Follow the current-firmware and backup steps. Save the downloaded files before continuing.
4. Supply a supported stock firmware file if the installer needs it for the codec.
5. Enter update mode only when prompted: switch off, hold **PTT** and **SK1** (the side key just below PTT), then switch on.
   The green LED lights in the bootloader.
6. Wait for writing and verification to finish. Follow any instruction to restart or reconnect.
7. Open **CPS**, read the radio, check your DMR ID and channels, and save a codeplug file.

If migration cannot recover your channels, import your saved codeplug in CPS and check the conversion before writing.
Manufacturer CPS files are not interchangeable with every supported import format. CPS identifies the file and reports what it can use.

## Update

Start with DM32+ running normally and choose **Update**. Connect, choose the desired release channel, and follow the
installer's instructions. Save a current codeplug before updating. Check the installed version in
**Settings > Advanced > About** afterward.

Choose **Edit build** to enable or disable firmware features before upgrading. The existing selection is the starting point.
Review choices for features added since your installed build. **Keep build** returns to preserving the installed selection.

The [Releases page](https://github.com/gjdothard/DM32Plus/releases) contains this project's downloads and release notes.

## If something goes wrong

Do not repeatedly write unrelated images or erase calibration data. Close applications holding the port, check the cable,
and follow [Troubleshooting](20-troubleshooting.md). If the new firmware will not start, the original bootloader remains
the recovery path: enter update mode and use a compatible image or your stock firmware.

Keep your stock firmware and stock codeplug for returning to the manufacturer's software.
Advanced migration and serial-loader procedures are in the [source reference](../../../user/19-moving-from-stock-dm32.md).

## Advanced: rfoundry on the command line

If you download a manual install kit, extract the entire kit and follow its generated README. Its commands refer to
the included image, manifest, tools, and any codec source you selected. Keep those files together.
Use the serial-cable procedure for this radio. Do not substitute the native-USB DFU instructions from another model.

**Use my own firmware build** accepts a locally compiled image and its optional manifest. An uploaded image is used
as supplied. Changing the selected options discards a prepared or uploaded image; load the matching file again or
let the service build the new selection.

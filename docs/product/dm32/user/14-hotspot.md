<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Hotspot and host connections

DM32+ communicates with a computer through the USB-to-serial programming cable.
Close CPS and the installer before another application opens the port. The cable's USB interface belongs to the adapter;
the radio itself is not a USB serial device or USB network adapter.

## DMR hotspot

Hotspot mode presents an MMDVM DMR modem to a host such as MMDVMHost.
Enable **Settings > Connections > Hotspot allowed**. Configure the host for the cable's serial port, UART at 115200 baud,
simplex DMR, and the intended frequency and color code. Enable network slot 2 and disable slot 1.
The firmware carries DMR frames between the host and RF; it does not need the AMBE codec for this mode.

The radio enters hotspot mode when the host sends its MMDVM requests. The hotspot screen shows state, frequency,
power, and the current or last call. Use **Tools & Utilities > Hotspot** for status and the stop action.
The host can transmit when connected, so check its configuration before starting it.

## Serial networking

The firmware implements PPP networking over the programming UART. This requires a configured PPP peer on the computer;
plugging in the cable does not create a network adapter automatically.
Browser CPS and PPP cannot own the same serial port at the same time.

The [PPP specification](../../../spec/ppp.md) describes setup and the services carried over that link.
USB-network instructions for CDC-NCM adapters, USB mode switching, and native-USB discovery do not apply to this radio.

## Host audio

USRP audio over UDP for bridges such as AllStarLink or DVSwitch is build-dependent. It uses an IP connection
such as serial PPP, not a USB sound device. See the [host-audio specification](../../../spec/usb-audio.md)
for configuration and limits.

## Advanced repeater modes

Standalone single-frequency repeater roles are implemented: a configured DMR channel can repeat from one TDMA
slot to the other on the same frequency without a computer host. User roles are also available. This experimental
path is separate from MMDVM SFR, where a host controls repeating. Review the codeplug role, inbound slot, and
framing before use. End-to-end hardware reliability is not established merely by enabling the role.

Split duplex operation has separate configuration and hardware requirements.
They are not the same as a simplex hotspot. Consult the [SFR specification](../../../spec/sfr.md),
[split-duplex specification](../../../spec/split-duplex.md), and
[hardware verification record](../../../testing/dm32-bringup.md) before experimenting.

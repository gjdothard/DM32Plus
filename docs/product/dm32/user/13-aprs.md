<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# GPS, APRS, and data

Open **Main Menu > GPS, APRS, and Data** (**OK**, **7** from Home).

| Card | Opens |
|---|---|
| **1 — Position** | Position, GPS fix, and satellites |
| **2 — APRS** | APRS status, position source, and beacon controls |
| **3 — ARS & LRRP** | Registration and GPS-request status |

## Position and time

Enable GPS in **Settings > Location** and allow a fix. Position pages distinguish a GPS fix from a configured fixed position.
The clock uses GPS or retained hardware time. Set **UTC offset** for the displayed local time; change it when your local offset changes.

## APRS

Set your radio name to begin with your callsign. The APRS page shows whether the callsign, position, and route are usable.
Choose the region/frequency and **Send via** route before enabling automatic beacons. AFSK sends on the configured FM APRS frequency;
DMR sends to the configured DMR gateway destination.

| Control | Use |
|---|---|
| Send position now | Request a single position transmission |
| Automatic beacons | Enable or disable scheduled beacons |
| Frequency | Select the regional APRS frequency |
| Power and Path | Set the FM beacon power and digipeater path |
| Interval and SmartBeaconing | Control periodic and movement-based beacon timing |
| Position | Select the position source and inspect the fixed position |
| Stations heard | Inspect decoded APRS stations |

Check the frequency and destination before transmitting. APRS requires appropriate amateur authorization.
The firmware waits for a clear channel; an unavailable position or invalid callsign can prevent sending.
To receive FM APRS, tune an FM channel or VFO exactly to the configured APRS frequency.

## ARS and LRRP

The dashboard shows registration attempts, retry/refresh state, and active location requests. Opening it does not send a packet.
**Register Now** schedules registration when the channel is eligible and clear.
Edit **Server ID** to select the registration destination; **0** disables ARS.

**Answer GPS requests** controls LRRP replies. Turning it off clears active subscriptions and pending reports;
turning it back on permits new requests. Subscription cards show the requester, interval, optional distance threshold,
and the next check or GPS status. A next-check time is an estimate.

## Computer connections

APRS and these DMR data services are separate from P25 operation; P25 channels cannot start APRS.
Host data services use the programming cable. See [Hotspot and host connections](14-hotspot.md).
The shared [APRS specification](../../../spec/aprs.md) documents protocol details and advanced configuration.

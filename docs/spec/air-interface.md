<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Air-interface integration contract

The core frames digital modes through `hal_air_t` rather than DMR slot events.
A board may expose `board_t.air` and discriminator/modulator samples through
`board_t.baseband`. Either pointer may be NULL. The protocol engine must refuse
an absent provider or a profile that provider cannot carry. See [P25](p25.md)
for current integration status and the [feature matrix](../feature-matrix.md)
for board support.

## Ownership and permission

A session has one owner. The radio core must stop competing DMR work before
opening it, route modem interrupts and deferred work to that owner, and restore
FM/DMR operation after closing it. Provider capability is not permission to
transmit. Channel RX-only, system listen-only, band, timeout and radio admission
checks remain above the provider. In particular, automatic trunked signaling
requires the same channel/system transmit permission as voice.

The provider must keep raw receive out of the speaker and microphone/beeps out
of the modulator while owned. It may key only within permission granted by the
radio core. Failed acquisition must leave no owner or armed PA. Stop, retune,
abort and close must discard stale queued work. Release must be idempotent.

## Data and execution

`hal_air.h` defines packed dibits, profile fit, symbol chunks, timing and events.
`hal_baseband.h` defines signed sample taps. `core/air` owns symbol framing,
session lifecycle and the sample-to-symbol soft modem. Protocol modules own
their data units, calls and channel policy.

Interrupts copy bounded hardware data and notify deferred work. Protocol
callbacks run at task level, never inside the hardware interrupt. Time comes
from the caller; engines use no RTOS calls or blocking waits. Memory is supplied
by the owner, with lifetimes covering outstanding callbacks and queued events.

## Current provider checks

`boardAirTest` verifies missing/unsupported/busy providers, RX-only capability,
retuning and release through the real session. `hrc7000AirTest` checks simulated
register/buffer behavior: receive-copy order, overrun, transmit half selection,
underrun, abort, suspend/resume, close/reopen and DMR reuse of shared memory.
The imported provider remains without a platform owner; these tests establish
host behavior, not target timing or radio interoperability.

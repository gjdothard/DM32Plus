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
The platform now exports `hrc_air` and `hrc_baseband` through the modem glue.
`modemGlueTest` executes the real glue and drivers with simulated IRQ/RTOS
services, including a deferred poll at the first scheduler resume. Air and
split-hotspot support are tested independently on and off.

Acquisition requires active DMR hardware with its stack stopped, no DMR TX,
and no FM receive-copy hook. The scheduler and modem interrupts remain held
while the old queues become the new state. Aligned storage and size assertions
cover both overlays. While leased, every DMR queue/BS operation refuses work;
the deferred task never drains that memory as DMR events.

An air TX fit requires both `air_tx_gate` and `air_tx_release` board hooks.
The gate must isolate speaker PCM/beeps from the modulator. It stays acquired
until deferred TX completion; another transmission cannot start while an old
completion is pending. Forced close releases it after RF stops and discards
callbacks. A late PA-timer interrupt only keys if TX is still active. These
hooks do not replace channel/system permission.

Task-side provider wrappers use `.flashfunc`; the linker keeps them out of
interrupt RAM. The deferred modem task, DMR HAL methods, initialization and
diagnostic formatting also use flash. Interrupt callbacks, shared locking and
the PA's TX-active state read remain in interrupt RAM. A board can now attach the PCM isolation callbacks and provider
pointers. Host results do not establish target timing or radio interoperability.

## PCM isolation and APRS coexistence

`hrc_audio_pcm_air_gate` closes ring playback and discards queued speaker
blocks. Microphone capture and the block callback continue, so the vocoder can
encode. `hrc_audio_pcm_ring` keeps the latest route request while gated and
applies it after release. Gate/release are serialized with each FIFO push.

The callback writes separate scratch storage. It publishes a speaker block
only if its stream and gate generation still match; an encode finishing across
a gate edge or stream restart cannot requeue stale samples. Space is checked
after the callback, so a full queue at entry can accept a block if space became
available. Turning speaker routing on from the callback retains its first block.

`hrc_audio_pcm_hold` now reports failure when air TX owns the prompt path. APRS
checks that result before changing the board route. Conversely, the air gate
refuses while APRS holds the FIFO, including its drain interval. A failed APRS
board route releases the hold. Neither owner steals the other's path.

`pcmGateTest` runs the actual PCM worker and APRS TX-start path on simulated
hardware, with air and VOX independently on/off. `modemGluePcmTest` links the
actual modem lifecycle to the real PCM gate/release callbacks.

With `CONFIG_AIR`, a dedicated 1 ms timer refills the speaker FIFO independently
of codec callbacks. It primes two 20 ms blocks, then pushes at most one 32-sample
chunk per interrupt. After observing room, it defers at least 200 us before
pushing; it never spins or invokes a codec in the interrupt. Stream stop, air
gating and APRS ownership disable refill. The feature-off path retains task polling.

The optional `hal_audio_t.spk_pending` reports queued non-silent audio and an
8 ms allowance after its last FIFO push. Natural P25 and DMR call completion
keeps speaker routing until this tail drains. Silence does not keep a call open;
forced stop or gating discards the queue immediately. Providers without this
hook retain immediate completion. Priming adds one block of startup buffering.

The FIFO simulation checks a 30 ms callback, bounded writes, clock wrap, tail
drain and ownership changes. Its two-half FIFO model is not a measurement.
Physical timing, sustained codec throughput and target budgets remain
qualification work.

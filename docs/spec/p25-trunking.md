<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 trunked channel operation

Status: experimental software integration. `CONFIG_P25_TRUNK` connects the
stored trunked channel owner to normal channel selection and PTT. Hardware
interoperability and audio/RF qualification remain open. Global build defaults
are off; full board builds may enable P25 and trunking. See the [feature matrix](../feature-matrix.md) for
board capabilities and [P25 settings](p25-codeplug.md) for stored records.

## Receive

The owner loads a system's identity, control channels, identifier plans and
selected talkgroup. Up to 16 control channels and 16 identifier rows are
supported by the stored loader. The existing follower hunts candidates,
validates the system identity, holds the control channel, follows desired
Phase 1 grants and returns after call end or voice loss. Its hold timer uses
the stored system setting. It can follow every group when configured.

Retunes run on the radio task. Each frequency must pass the board's receive
band checks and RF provider. A refused retune admits no units from the old
frequency. Audio hooks accept voice only after a confirmed voice-channel
retune. Encrypted and TDMA grants are skipped. Unknown identifiers cannot
authorize transmission. Phase 2, encryption and LSM/CQPSK are outside scope.
Stored fallback selection is not activated by this owner.

## Transmit admission

| Stored permission | Behavior |
|---|---|
| Channel Receive only | No PTT TX, registration, affiliation or voice requests |
| System listen-only | Same emission refusal, even if the channel allows TX |
| Permitted TX with NO_REG | PTT may request voice; no registration or affiliation |
| Permitted TX with registration | Register, affiliate, request and use a valid voice grant |

The strict admission reader supplies separate registration, request and voice
permissions. Missing, stale, malformed or future permission records grant none.
Settings generations invalidate the entire session, including queued bursts.

Provider callbacks queue retunes and signaling. They never key RF. The radio
task holds the storage lock while preparing each burst or voice transmission,
checking current permissions and frequency, invoking normal radio admission,
keying and starting modulation. Normal admission includes inhibit, critical
battery, busy-channel lockout, power and band checks. Active voice applies the
normal timeout. Host-stream PTT remains refused.

PTT requests a channel rather than keying the stored base frequency. Only a
current grant to this unit and talkgroup can start voice, on the grant's
permitted transmit frequency. PTT release before the grant cancels the request.
Release during voice sends the terminator, unkeys and returns to control.
Failed preparation, keying or modulation start aborts queued symbols. Revoking
permission while waiting or transmitting closes and unkeys the owner on its
next radio-task poll. Future mode-aware external writers must quiesce the
owner before acknowledging permission updates.

## Memory and concurrency

The follower, configuration arrays, pending work and keying callback live in
the exclusive codec-state loan. The large stored-loader output shares voice
scratch space only during startup, before voice processing begins. No second
permanent trunking allocation or private-codec memory change is made.

The provider and radio task use the upstream nonblocking single-core exclusion
protocol around follower mutation. A provider that preempts the radio task
skips follower work; it never waits. Teardown closes the provider before ending
the follower, stopping PCM callbacks and returning the loan.

The radio task samples its follower clock only after acquiring that exclusion.
Otherwise a provider can preempt after the clock read and refresh `t_good` with
a later timestamp; unsigned elapsed-time arithmetic then falsely reports a
control or voice timeout. After a receiver retune completes, the task samples
the clock again before acknowledging the retune. Time spent in the RF provider
does not consume the new channel's receive window. Genuine receive-loss
timeouts retain their existing durations.

## Visibility and checks

The debug core view exposes `p25.trunk.state` (0 off, 1 hunt, 2 control, 3 voice),
`p25.trunk.reg` and `p25.trunk.req`, using the follower's enums. These values are
published by the radio task; debug readers never access loaned state directly.
The [P25 monitoring UI](p25-ui.md) shows talkgroups, source IDs and system
details, with receive-only listening filters and persistent favorites.
Legacy codeplug writers continue refusing tagged codeplugs.

`radioP25TrunkStateTest` connects real stored records, state-machine dispatch,
follower, framing and voice with simulated hardware. It checks passive follow
and return, allowed subscriber signaling and granted voice, cancellation,
permission revocation, invalid frequencies, encrypted/TDMA grants, failed start
and normal inhibition. The owner tests also inject decoder callbacks between
clock sampling and task resumption, and simulate a slow RF retune. They check
that both channel types remain held without false loss and that genuine voice
silence still times out. Existing pure follower tests cover noisy symbol streams,
queued/denied requests and access timers. Read-only HIL visibility is supplied
in `tests/hil/test_p25.py`; running it does not qualify RF or audio.

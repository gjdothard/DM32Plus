<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 integration tracker

Updated: 2026-11-18. Branch: `codex/p25-integration`.
This is the working plan and progress record. Update it in every integration
commit: completed task IDs, evidence, remaining work, and the next task. Mark a
milestone complete only after its acceptance checks pass. Host, target-build,
bench and interoperability evidence are separate.

## Scope agreed with the owner

Support conventional and trunked P25 Phase 1 receive and transmit using the
MIT RadioFoundry implementation. Preserve the existing CPS receive-only option
and normal transmit protections. Do not turn the whole implementation into a
receive-only product. No particular trunked system is the acceptance target.

| Channel | CPS RX-only | System permits TX | Expected behavior |
|---|---|---|---|
| Conventional | yes | not applicable | Receive; PTT refused; no RF TX. |
| Conventional | no | not applicable | PTT can transmit after normal checks. |
| Trunked | yes | either | Follow calls; no registration, affiliation, service request or PTT TX. |
| Trunked | no | no (listen-only) | Follow calls; no automatic signaling or PTT TX. |
| Trunked | no | yes | Preserve upstream registration, affiliation and granted-call TX, subject to normal checks. |

The RX-only rule applies before PTT as well as during it. A missing or invalid
record must not grant transmit permission. Preserve these choices through save,
load, copy, mode changes, CPS round trips and restart. Reapplying RX-only while
requests or audio are queued must cancel the old session and release ownership
without leaking queued signaling or leaving the PA armed.

Phase 2, LSM/CQPSK, encryption, additional RF bands and NXDN integration are
outside this effort. Do not import the GPL fork or private Next implementation.
Upstream's active trunking is not live-system qualified; retain that distinction
when exposing it in this product.

## Baselines

| Item | Revision |
|---|---|
| Main used to create the branch | `ca28180dd4e21789f2627742359ff03221d8c924` |
| Pinned RadioFoundry implementation | `c34dc440aaf2d3952458bc0bd85b8c56c76f6b23` |
| Foundation import | `ae0ade75` |

See the [assessment](dm32-p25-plan.md),
[import provenance](../../core/p25/UPSTREAM.md) and
[current subsystem specification](../spec/p25.md).
The histories have no shared ancestor. Integrate reviewed groups of changes;
preserve DM32Plus history, product identity, custom UI and release configuration.

## Milestones

| ID | Status | Work | Acceptance gate |
|---|---|---|---|
| M0 | Complete | Branch, revision pins, clean-room/provenance review. | Branch based on main; no foreign/GPL implementation imported. |
| M1 | Complete (host) | FEC, air framing/session/soft modem, IMBE, P25 engines and follower. | Eleven host configurations pass with warnings as errors; notices retained. |
| M2 | In progress | Build switches, optional board interfaces, modem/audio ownership and target baseline. | Default and feature-off builds clean; provider tests and target stack/RAM checks pass. |
| M3 | Queued | Versioned channel/system records and RX-only admission. | Existing records preserved; real record decoder passes the permission matrix above. |
| M4 | Queued | Conventional P25 radio-core receive and transmit. | Stored-channel RX/TX, startup/failure cleanup, RX-only PTT refusal and FM/DMR transitions pass host integration tests. |
| M5 | Queued | Trunked acquisition, grant following and active subscriber path. | Hunt/hold/follow/return and allowed TX pass simulated-site tests; RX-only blocks every signaling path. |
| M6 | Queued | Product UI and companion CPS. | Mode/settings editing, capability/version checks and firmware/CPS round trips work; translations and gallery reviewed. |
| M7 | Queued | Full regression, configuration matrix and packaging. | Host/sanitizer suite, zero-warning firmware matrix, stack/RAM limits and codec-free package checks pass. |
| M8 | Needs owner-authorized hardware session | Qualification against independent equipment and representative systems. | RX, conventional TX and trunking behavior measured; RX-only has no emissions; qualification limits recorded. |

Dependencies: M2 before radio operation; M3 before exposing stored P25 channels;
M4 before M5; M3/M4/M5 before M6 acceptance; M7 before release qualification.
UI/CPS schema design can proceed while provider work is being tested.

## Current work: M2

- [x] M2.1: Add optional `board_t.air` and `board_t.baseband` contracts. Existing
  board initializers omit them and remain without providers.
- [x] M2.1: Exercise missing/unsupported/busy providers, successful acquisition,
  retune and idempotent release through the actual air-session implementation.
- [x] M2.2 (host checked; target build pending): Add coordinated CONFIG switches and source selection; keep product
  activation deferred until the radio core and storage use them. Retain the
  IMBE About-page attribution when the codec is compiled.
- [x] M2.3 (host): Port the HR_C7000 air provider and its required DMR-engine ownership
  changes together. Route interrupts and deferred events through the owner;
  release cleanly on failed open, stop, retune and mode change.
- [x] M2.3a: Import the provider and its simulated register tests from the pinned
  upstream. Keep the current DMR driver; the provider passes against it.
- [x] M2.3b (host): Wire platform ownership, interrupts and deferred work. Protect
  shared queues against DMR/BS calls and scheduler preemption; serialize audio
  gate release with TX completion; reject stale PA-timer keying after close.
- [x] M2.3c (host): Attach board provider pointers and real audio-gate callbacks.
  Board/radio sources compile with P25 on/off and DMR off using fake CPU services.
- [ ] M2.4: Port audio scheduling and memory/stack changes required by that
  provider. Preserve FM/DMR/APRS operation and the private AMBE slot.
- [x] M2.4a (host): PCM gate/release, latest ring request, stale-block rejection,
  callback scratch storage, and exclusive APRS hold. Test P25/VOX on/off and
  link the actual PCM gate into the modem lifecycle test.
- [x] M2.4b (host scheduling): Add timer-driven FIFO refill independent of codec
  callbacks, bounded writes, deferred settling and two-block priming. Natural
  P25/DMR completion waits for queued non-silent speaker audio to drain.
- [ ] M2.4b (target qualification): Measure FIFO behavior, sustained codec
  throughput, stack/RAM and image cost. The simulated FIFO model is not measured
  hardware behavior.
- [ ] M2.5: Run simulated modem/provider tests and obtain a reproducible C-SKY
  firmware build with measured RAM, stack and image size. Build affected
  switches off before marking M2 complete.

Next task: M2.4b/M2.5, a reproducible target build and resource checks. Do not
advertise P25 in VERSION, the UI or CPS until the matching integration gates pass.

## Required test cases to carry through later stages

- Conventional: clear calls, NAC/group filtering, encode/decode, PTT start/end,
  timeout, microphone/speaker routing and RX-only refusal.
- Trunked RX: control-channel hunting/loss, identifiers and frequency checks,
  desired grants, encrypted/TDMA rejection, call end, voice loss and return.
- Trunked TX: permitted registration/affiliation, queued/denied/timed-out voice
  request, valid grant, granted TX, release and return. Never key a stale grant.
- RX-only: automatic startup/tick without PTT; PTT on control and voice; saved
  and copied records; restart; system listen-only; RX-only applied while waiting
  for a grant or holding queued TX. Check send callbacks and actual radio/PA
  admission, not just a UI message or one follower flag.
- Existing product: FM/DMR calls, APRS, talkgroups, storage preservation, home
  and quick controls, navigation, serial CPS and packaging regressions.

## Evidence and work log

| Work | Evidence | Limit |
|---|---|---|
| M0/M1, `ae0ade75` | Exact-source audit of 60 imported files; 11 host configurations passed with Zig 0.14.1 and `-Wall -Wextra -Wshadow -Werror`. | Includes trunking on/off and passive PTT; not board/radio/CPS integration. |
| M1 documentation | 153 Markdown files, zero broken links; generic-code check passed. | Full CI and target build not run. |
| M2.1, `71f2367a` | `boardAirTest`: 20 checks passed; existing `boardBandTest` passed, warnings as errors. | Simulated provider; no board driver enabled or hardware touched. |
| M2.2 | Actual Make selection plus C compile/link matrix for default-off, P25-only, P25+trunking and explicit-off; invalid dependencies and overrides rejected; conditional license attribution and idempotence checked. | Host compile/link, not a target image. |
| M2.3a, `a8de90c0` | `hrc7000AirTest`: 13,306 simulated checks passed with the current DMR driver. Provider-off object build checked by `p25BuildTest.py`. | Initial driver import; subsequent platform ownership is covered by M2.3b. |
| M2.3b | Real glue/driver lifecycle tests with air and split support independently on/off: 94, 11, 100 and 11 checks passed. Optimized builds checked. Existing DMR, split-DMR and BS-driver regressions passed. DMR-disabled glue compiles in the build matrix. | Simulated IRQ/RTOS services; audio-gate callbacks are fakes. Board attachment, target memory/timing and hardware behavior remain pending. |
| Combined host Make run | `run-p25`: 13 executable configurations and 5 Python build/attribution tests passed with warnings as errors. Documentation: 155 files, zero broken links; generic-code check passed. | No C-SKY target build, sanitizer run, complete host suite or hardware session yet. |
| Combined run after M2.3b | `run-p25`: 17 executable configurations and 6 Python build/attribution tests passed, including the optimized handoff tests. Documentation: 155 files, zero broken links; generic-code check passed. | Target builds, full CI, board/audio attachment and hardware qualification are still pending. |
| M2.3c/M2.4a | PCM/APRS tests: 50/16/50/16 checks for P25/VOX on/off. Modem lifecycle linked to the real PCM gate: 96 checks. Changed board/radio files compile with P25 on/off and DMR off. | Simulated CPU/IRQ/RTOS services; native compiler checks are not a C-SKY image or hardware qualification. |
| Combined run after PCM attachment | `run-p25`: 22 executable configurations and 7 Python build/attribution tests passed with warnings as errors. Documentation: 155 files, zero broken links; generic-code check passed. | Hardware-paced refill, target resource checks, full CI and radio/CPS channel integration remain pending. |
| M2.4b host refill/drain | PCM/APRS: 246/17/246/17 checks for P25/VOX on/off; includes clock wrap, a 30 ms callback, bounded IRQ writes, ownership and silent-tail completion. P25 voice: 66 checks; DMR voice: 107 checks. | Timer 6 and two-half FIFO behavior require target validation; priming adds one block of startup buffering. |
| Combined run after refill | `run-p25`: 23 executable configurations and 7 Python build/attribution tests passed with optimized warnings-as-errors builds. | No C-SKY target build, complete host suite, full CI or hardware qualification. |
| Existing audio/radio regressions | Radio core/features/receive bands/roaming/USRP: 2181/892/262/216/121 checks passed. Board/radio attachment compiles with P25 on/off and DMR off. Documentation: 155 files, zero broken links; generic-code check passed. | Host compiler with fake CPU services; not target resource or hardware evidence. |
| Scope update | Owner requested retaining TX and honoring existing CPS RX-only; permission matrix above replaces the earlier passive-only product scope. | Hardware TX still requires its own authorized test session and --allow-rf. |

Local ignored test output is in `build/p25-host`. Python works outside the
sandbox; the earlier launcher failures were sandbox access failures. A portable
GNU Make binary is now available under ignored `build/host-tools`, downloaded
from MSYS2 with its published SHA-256 verified. It uses Git's existing runtime;
no system installation or firmware-source import was involved. Portable Zig
is available for host checks. The Windows host Make run overrides the existing
ELF attribute-stripping CFLAGS, which otherwise break Windows runtime inlines;
warnings remain errors. WSL is not installed and C-SKY GCC is not on PATH.
Establish the target toolchain before M2 acceptance;
do not equate host success with firmware fit.

No branch push, deployment, CPS/radio session or hardware test has been performed.
Keep this record current as those facts change; remove temporary CI build trees
after successful runs as required by AGENTS.md.

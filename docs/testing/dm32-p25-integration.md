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
- [ ] M2.4b (target qualification): Measure FIFO behavior and sustained codec
  throughput. Static stack/RAM/image baselines are recorded below; active-mode
  state ownership remains open. The simulated FIFO model is not measured hardware.
- [x] M2.5a: Establish a local C-SKY build, measure default/P25 links, and
  resolve the interrupt-RAM overflow without shrinking the heap or codec slot.
- [x] M2.5b: Check default, conventional-only, trunking-enabled and DMR-off
  target builds. Check static task stacks in default and P25-enabled firmware.
- [ ] M2.5c: Define and test a safe home for caller-owned P25 state. A successful
  link with no channel owner does not establish active-mode memory fit.

Next task: M2.4b/M2.5, target resource checks and exclusive state-memory ownership. Do not
advertise P25 in VERSION, the UI or CPS until the matching integration gates pass.

## Target resource baseline (2026-11-18)

The existing local QEMU builder runs GCC 14.2.0 in
`ghcr.io/radiofoundry/rfoundry-csky-gcc14:20261011b`, image ID
`sha256:4be36b7e30efa70f69d6e4a94223e734ad861a368111e44dbdcdaa8fcb44bede`.
The build uses `52df41a4` plus this milestone's task-placement and DMR-off
dashboard guard changes. FreeRTOS is clean at the pinned
`3a22924e0a9ddbbc8b0758881c33b3422a5cc20d`. No codec file is included.

Before the placement fix, default RAM usage was 211,440 bytes and P25 overflowed
by 2,152 bytes (213,608 requested). Moving task-only glue and diagnostics to
flash resolves the overflow. IRQ callbacks, locks and the PA state read remain
in RAM. No heap, startup stack or private codec reservation was reduced.

| Target configuration | Flash bytes | Main RAM bytes | Static RAM headroom |
|---|---:|---:|---:|
| Default, P25 off | 634,988 | 209,208 | 2,248 |
| Conventional-only P25 | 645,248 | 210,976 | 480 |
| P25 with trunking | 645,240 | 210,976 | 480 |
| DMR off, P25 off | 336,352 | 129,152 | 82,304 |
| Diagnostic retained-entry-point ELF | 675,616 | 211,120 | 336 |

The application flash budget is 901,120 bytes; main RAM is 211,456 bytes.
All four firmware builds use `-Os -Werror` and contain no compiler warnings.
The diagnostic row is not a product image. Its caller-owned state is absent.

The P25-enabled static stack check passes all ten tasks. After the tool's
68-byte context overhead and 128-byte margin, audio retains 1,592 bytes,
the modem task 688 bytes and the radio task 840 bytes. UI is the smallest
at 40 bytes beyond that margin. These are static estimates, not measured
stack high-water marks or an active-channel qualification.

The compile-only `p25-resources` target reports these target ABI sizes with
trunking enabled:

| Caller-owned state | Bytes |
|---|---:|
| `p25_voice_mem_t` | 4,476 |
| `p25_engine_t` | 872 |
| `p25_follow_t` | 452 |
| `air_session_t` | 196 |
| `air_softmodem_t` | 372 |

Without trunking, the engine is 860 bytes and no follower marker is emitted.
With P25 off, no state markers are emitted. The same build directory was
checked across all three settings to verify the probe is always recompiled.

These are separate type sizes. They are not all incremental allocations:
the provider already overlays the soft modem with idle DMR queue storage.
The unconnected voice/engine/follower owner does not allocate its state yet.
The current vocoder HAL has no memory-lending interface. M2 must establish
exclusive ownership and restoration before using any idle codec memory;
the private AMBE slot and reserved window remain intact.

Within the build container, use distinct build directories and fixed version data:

```sh
make -j4 BOARD=dm32 BUILD=build/p25-target/default GITVERSION=52df41a4 GITDIRTY=-dirty RFOUNDRY_GIT_TIME=1795019138 RELEASE= OPT='-Os -Werror'
make -j4 BOARD=dm32 BUILD=build/p25-target/p25 CONFIG_P25=y CONFIG_P25_TRUNK=y GITVERSION=52df41a4 GITDIRTY=-dirty RFOUNDRY_GIT_TIME=1795019138 RELEASE= OPT='-Os -Werror'
make BOARD=dm32 BUILD=build/p25-target/resources CONFIG_P25=y CONFIG_P25_TRUNK=y OPT='-Os -Werror' p25-resources
```

For conventional-only use `CONFIG_P25=y CONFIG_P25_TRUNK=n`; for the DMR-off
check use `CONFIG_DMR=n CONFIG_P25=n`, each in its own directory. The latter
exposed a dashboard access to absent `radio_view_t.dmr` fields. Guarding that
block fixes compilation; the DMR-enabled rendering is unchanged. A host
compile regression now covers the feature-off dashboard.

An enabled image can prune unreferenced P25 functions. A diagnostic ELF also
retains voice, conventional TX and follower entry points with linker `--undefined`
options. It still does not allocate caller-owned state or simulate a channel.
The retained roots are `p25VoiceAudioBlock`, `p25VoiceInit`, `p25VoiceBegin`,
`p25VoicePoll`, `p25VoiceTx`, `p25VoiceEnd`, `p25_engine_begin`,
`p25_engine_tx_prepare`, `p25_follow_begin`, `p25_follow_tick` and `p25_follow_ptt`.
This is a selected-entry-point diagnostic, not an all-code size bound.
Linker headroom is static space, not free RTOS heap, and is not permission to
put the voice state there. Hardware FIFO timing and sustained codec throughput
remain unmeasured.

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
| M2.5 target build/resource milestone | Default, P25, conventional-only and DMR-off C-SKY images build with `-Werror`; default and P25 static task-stack checks pass. Retained-entry-point ELF and target ABI probe expose current code/state costs. Task-only placement fixes a 2,152-byte RAM overflow. | Caller-owned P25 state is not allocated yet; memory ownership and restoration remain open. |
| Host checks after target placement | All 23 `run-p25` executables and 9 Python build/attribution/probe/feature-off checks pass. | Full CI and owner-authorized hardware qualification remain pending. |
| Scope update | Owner requested retaining TX and honoring existing CPS RX-only; permission matrix above replaces the earlier passive-only product scope. | Hardware TX still requires its own authorized test session and --allow-rf. |

Local ignored test output is in `build/p25-host`. Python works outside the
sandbox; the earlier launcher failures were sandbox access failures. A portable
GNU Make binary is now available under ignored `build/host-tools`, downloaded
from MSYS2 with its published SHA-256 verified. It uses Git's existing runtime;
no system installation or firmware-source import was involved. Portable Zig
is available for host checks. The Windows host Make run overrides the existing
ELF attribute-stripping CFLAGS, which otherwise break Windows runtime inlines;
warnings remain errors. WSL is not installed and native C-SKY GCC is not on PATH.
The existing localhost-only build VM now provides target compilation. Target
logs and artifacts are kept under ignored `build/p25-target`; the VM workspace
is `/home/builder/p25-integration-52df41a4`. No hardware service was accessed.
Do not equate a pruned firmware link with active-mode memory fit.

No branch push, deployment, CPS/radio session or hardware test has been performed.
Keep this record current as those facts change; remove temporary CI build trees
after successful runs as required by AGENTS.md.

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
receive-only product. The owner confirmed VHF/UHF P25 as the hardware target
after identifying Palmetto 800 around Goose Creek, Summerville and Berkeley
County as an intended use. Palmetto 800 is outside the documented DM32 RF
bands and is not an acceptance target for this port. Firmware work does not
establish 700/800 MHz reception. Control channels and grants must pass the
board's frequency checks before tuning or TX admission.

Sources for this scope decision: the state's
[Palmetto 800 overview](https://www.admin.sc.gov/services/technology-services/palmetto-800)
and the manufacturer's
[DM-32UV manual, Appendix B](https://baofeng.s3.amazonaws.com/Baofeng_DM-32UV_User_Manual_20250210.pdf).

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
| M3 | In progress | Upstream tagged channel/system records and RX-only admission. | Existing records preserved; real record decoder passes the permission matrix above. |
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
- [x] M2.5c (host and target ABI): Add exclusive codec-state loans and a grouped
  P25 workspace that fits the existing encoder-state pool. Reject busy/stale
  ownership, preserve codec identity/options and reinitialize AMBE after return.
- [ ] M4 handoff gate: Connect the workspace lease to the actual mode owner,
  stop every old voice/air callback before return, and test real mode transitions.

Next task: M3, versioned channel/system records and the RX-only permission matrix;
then M4 will consume the workspace lease during mode changes. M2's physical FIFO
and throughput qualification still needs an owner-authorized hardware session. Do not
advertise P25 in VERSION, the UI or CPS until the matching integration gates pass.

## Current work: M3 and reuse strategy

Before implementing each remaining layer, compare the pinned upstream's code,
spec and tests. Reuse its format and implementation where possible; adapt only
the local storage map, product UI, CPS behavior, RX-only admission and stricter
memory ownership. Batch target links after backend changes stabilize. Use
focused host/configuration tests during pure-codec and format work.

- [x] M3.1: Import the parameter-only portions of `p25_codeplug.[ch]` and all
  39 upstream P25 vectors (28 apply without trunking). No alternate record format.
- [ ] M3.2: Port `codeplug_kv`, `codeplug_tag`, `codeplug_modes` and
  `codeplug_records` with their simulated-flash and power-cut tests. Preserve
  the fork's talkgroup/AES extensions and foreign-data protection.
- [x] M3.2a (host): Reuse the upstream keyed store and its full test suite.
  Adapt local storage guards, preserve unknown data and serialize raw commits.
- [ ] M3.2b: Port the mode registry, tag layer and system readers. Before any
  channel activation, verify unsupported modes remain parked with features off.
- [x] M3.2b (configuration backend, host): Import the registry, tag and record
  readers. Keep every tagged kind unrunnable and use an explicit tagged API;
  the existing radio reader retains the safe legacy base.
- [x] M3.2b (generic host suite): Reuse the full upstream tag/record tests,
  including live/stale bindings, future kinds, full area, rewrites and shared
  targets/lists/systems. All 241 checks pass through the explicit adapter.
- [ ] M3.3: Restore upstream P25 storage loaders and adapt the logical channel
  model. Test malformed/missing tags, copies, deletes, saves and CPS round trips.
- [x] M3.3a (host): Restore full upstream loaders and stored-channel tests.
  Check logical RX-only edits, safe legacy reads, rename, copy, deletion,
  restart and stale-name replacement. CPS round trips remain pending.
- [x] M3.3b (sampled host power cuts): Interrupt RX-only save, rename, deletion,
  plain replacement and RX-only copy. Replay the actual journal; accept the
  old committed state or a safe new/intermediate state. Preserve the source
  during copy. This samples writes; it is not exhaustive per-byte qualification.
- [ ] M3.4: Prove channel RX-only and system listen-only block PTT and all
  automatic signaling. `NO_REG` is not receive-only permission.
- [x] M3.4a (record policy): Add separate voice/request/registration permission
  bits. RX-only, listen-only, missing targets, invalid resolved IDs, malformed
  mode bodies, unknown flags and read failures grant no record permission.
  Keep tolerant upstream receive defaults. Check the full `NO_REG` matrix.
- [ ] M3.4b (runtime): Wire the record policy into the real owner and every
  emission path, invalidate queued work on updates, and check actual grants,
  frequencies, board capabilities and existing radio protections.

The upstream tag writes the legacy base as receive-only DMR and reconstructs
logical P25 permissions from its TX flag. The existing CPS checkbox must edit
that logical permission through the P25 adapter; reading only the base bit is
insufficient. Do not activate tagged channels until this mapping is tested.
See [P25 codeplug integration](../spec/p25-codeplug.md).

Next task: connect the conventional radio owner and workspace lease, using
the strict record policy and normal radio protections. An old CPS
rewrite must not preserve a logical TX flag after the operator selects RX-only;
that transfer/capability contract must be resolved before exposing P25 editing.

M4/M5 should adapt upstream `core/radio/radio_p25.[ch]`, including its task
handoff, retune and queued signaling logic. Replace its simple memory-lending
call with the local exclusive lease API and measure the complete owner state;
the current minimum workspace does not include all future owner bookkeeping.
M6 should reuse upstream `site/static/js/codeplug/mode_p25.js` where compatible
with the companion CPS, preserving this fork's existing editor behavior.

## Initial target resource baseline (2026-11-18)

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
At this baseline the vocoder HAL had no memory-lending interface. The next
milestone below adds exclusive state ownership and restoration. The private
AMBE slot and reserved window remain intact.

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

## Workspace loan milestone

`68f77717` plus the memory-loan changes adds the optional HAL loan pair and
`p25_workspace.[ch]`. The provider lends the existing 7,168-byte encoder state;
it rejects an active codec call, another loan, invalid size/alignment or permanent
parking. Returns validate the exact pointer/capacity/generation. Reset is skipped
and encode/decode return zero output while lent. Fresh AMBE initialization after
return restores both encoder and decoder state. Codec identity and options stay
available; executable code, flash and modem tables are never lent or rewritten.

The target ABI probe reports a 5,800-byte trunked workspace or 5,336 bytes without
trunking, and a 16-byte caller-owned lease handle. The engine already includes
its air session. The target ELF confirms the existing encoder pool remains
7,168 bytes and the decoder pool 6,144 bytes. This leaves 1,368 bytes within the
encoder pool with trunking enabled; no second permanent workspace is allocated.

`vocoderMemoryTest` runs the actual wrapper and workspace helpers with C codec
stand-ins. Conventional/trunking/P25-off configurations pass 85/85/16 checks.
They cover blocked calls without changing borrowed bytes, real reset-path
reinitialization, options/identity, stale and malformed returns, unavailable
providers, rollback failures, absent slots and parking during acquisition.
The four existing radio suites pass 2,181/892/262/216 checks.

| Workspace-era target configuration | Flash bytes | Main RAM bytes | Static RAM headroom |
|---|---:|---:|---:|
| Default, P25 off | 634,996 | 209,208 | 2,248 |
| Conventional-only P25 | 645,704 | 210,992 | 464 |
| P25 with trunking | 645,696 | 210,992 | 464 |
| DMR off, P25 off | 336,352 | 129,152 | 82,304 |

All four target builds pass with `-Os -Werror`. The loan provider adds 16 bytes
of aligned main-RAM usage to the enabled image; the workspace itself reuses
the existing pool. The 16-byte lease handle remains the future caller's cost.
Use `BUILD=build/memory/<configuration>`, `GITVERSION=68f77717`, and the same
fixed timestamp/options as the baseline commands above. Target evidence is in
`build/p25-target`; the VM source tree is `/home/builder/p25-memory-68f77717`.
The enabled target's static stack check passes all ten tasks: audio retains
1,596 bytes beyond the context overhead/margin, the modem 688 bytes, the radio
840 bytes and UI 40 bytes. This does not measure runtime high-water marks or
the future channel owner's complete call chain.

The workspace API is available for M4 but has no radio-core channel caller yet.
The caller must stop AMBE before borrowing and stop P25 audio/air callbacks
before return. This change does not prove that those mode transitions are wired,
activate a P25 channel, or establish RF/voice interoperability.

## Tagged configuration backend milestone

The full pinned P25 loaders now use the shared tag/system readers through an
explicit configuration API. `codeplug_channel` still returns the legacy base;
the existing radio dispatch never receives a tag's logical TX permission.
No tagged mode is advertised or runnable. The new fields in `channel_t` do
not increase linked static RAM in the target configurations checked here.

| Backend-era target configuration | Flash bytes | Main RAM bytes | Static RAM headroom |
|---|---:|---:|---:|
| Default, P25 off | 634,992 | 209,208 | 2,248 |
| Conventional-only P25 | 645,696 | 210,992 | 464 |
| P25 with trunking | 645,692 | 210,992 | 464 |
| DMR off, P25 off | 336,352 | 129,152 | 82,304 |
| Diagnostic retained entry points and loaders | 682,044 | 211,136 | 320 |

The diagnostic link retains the previous voice/engine/follower roots plus
`p25_conv_load`, `p25_conv_save`, `p25_trunk_load`,
`codeplug_tagged_delete_channel` and `codeplug_tag_copy`. It has no unresolved
symbols. It still does not allocate the complete future radio-owner state or
prove an active channel fits. Normal builds discard unreachable loaders.

These builds use `f2ac5367` plus the backend changes, `GITVERSION=f2ac5367`,
`GITDIRTY=-dirty` and the fixed timestamp/options recorded above. Host tests
pass all 32 P25-suite executables and 11 Python build checks, plus both legacy
codeplug configurations. The imported generic metadata suite passes 241 checks;
the stored P25 suites pass 116 conventional and 158 trunking-enabled checks.
Generic lifecycle power-cut tests, strict runtime TX admission, feature-off
dispatch and CPS transfer behavior remain open.
All four normal target builds pass with `-Werror`. The P25-enabled static stack
check passes every task; UI remains the tightest at 40 bytes beyond the required
context overhead and margin. Logs, maps and the diagnostic ELF are archived in
`build/p25-target/codeplug-evidence.tar.gz`. No hardware was accessed.

## Record permission milestone

Local `p25_admission.[ch]` adds a record policy independently of the imported
receive loaders. It resolves the base/tag once under the storage lock and
allocates no permanent state. A strict keyed lookup distinguishes an absent
optional identity from a read error; the existing tolerant API is unchanged.
Runtime dispatch still has no P25 channel caller, and the kind registry stays
unrunnable. This does not complete M3.4's actual emission checks.

Host evidence: all 34 `run-p25` executables and 11 Python checks pass. The new
conventional/trunking suites pass 10,542/10,676 checks. Each configuration
samples 2,228 actual power cuts across five edit operations, plus successful
completion points. Tests inject a failure at each read position of the tested
conventional/trunked record-policy paths. These simulated reads and flash
units are not measured physical timing or hardware fault qualification.

| Admission-era target configuration | Flash bytes | Main RAM bytes | Static RAM headroom |
|---|---:|---:|---:|
| Conventional-only P25 | 645,700 | 210,992 | 464 |
| P25 with trunking | 645,692 | 210,992 | 464 |
| Diagnostic roots, loaders and permission policy | 683,096 | 211,136 | 320 |

Both enabled configurations pass with `-Os -Werror`; host checks also compile
P25/trunking off and DMR off. The diagnostic retains the previous roots plus
`p25_codeplug_tx_permissions` and has no unresolved symbols. The gate adds no
static RAM. It still has no task caller; its eventual full owner call chain
and complete state require M4 stack/RAM checks. Build inputs are `0e3a4a0d`
plus this milestone, with the same fixed options/timestamp as above and
`GITVERSION=0e3a4a0d`. Exported changed-source timestamps are refreshed before
incremental compilation; target source hashes match the exported inputs.
Evidence is in `build/p25-target/admission-evidence.tar.gz`.

P25 and trunking now have qualification catalog entries. The feature list uses
one declaration so the existing catalog reader sees both switches. Linux
`bringup.py check` reports zero errors; pytest is absent in that build container,
so it does not check test markers. Owner-authorized hardware checks remain open.

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
| M2.5c memory ownership | 26 `run-p25` executables and 10 Python checks pass; real codec-wrapper/workspace ownership tests pass with P25/trunking independently on/off. Target probe confirms the grouped workspace fits the existing encoder pool, and four firmware configurations build without warnings. | Codec stand-ins validate the wrapper, not proprietary codec behavior on hardware. The real channel owner has not acquired or released a workspace yet. |
| M3.1 upstream codecs | Exact-source audit: 14 parameter/helper functions and all 39 P25 vectors match the pin. Vector tests: 30 checks without trunking, 42 with it. All 28 `run-p25` executables and 11 Python checks pass with warnings as errors. | Parameter bodies only; generic storage, loaders, RX-only admission and CPS mapping remain pending. Target links are batched with the backend integration. |
| M3.2a keyed-store reuse | Full imported NOR/power-cut suite plus local guard/interleaving tests: 4,471 checks pass. Removing the raw-commit outer lock reproduces key loss and fails the new regression. All 29 `run-p25` executables, 11 Python checks and both existing codeplug test configurations pass. | Host only; no transfer caller or tagged channel is activated. Target linking/resource checks are batched with the tag/reader integration. |
| M3.2b/M3.3a metadata and loaders | Full upstream tag/record suite: 241 checks; stored P25: 116/158 checks. All 32 P25-suite executables, 11 Python build checks and both legacy codeplug configurations pass. Four target configurations and the P25 static stack check pass; diagnostic retained-loader link fits. Exact-source audit matches six imported implementation/header/vector files after only documented adaptations. | Explicit configuration API only. Lifecycle power cuts, strict TX admission, radio dispatch and CPS round trips remain open. |
| M3.3b/M3.4a record permissions | Conventional/trunked admission: 10,542/10,676 checks; 2,228 sampled cuts per configuration; per-read fault injection. All 34 P25 executables, 11 Python checks and both legacy codeplug configurations pass. Both enabled target configurations and the retained-permission link fit without added static RAM. Feature catalog: zero errors on Linux. | Record policy only. Actual emission gates, session invalidation, frequency/grant checks, CPS round trips, complete-owner resource checks and hardware qualification remain open. Catalog test markers were not checked because the build container lacks pytest. |
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

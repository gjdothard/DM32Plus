<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 upstream integration assessment

The working checklist, current scope and test evidence are maintained in the
[integration tracker](dm32-p25-integration.md). The owner has expanded the
initial passive-only scope: retain conventional and trunked TX while honoring
the existing CPS RX-only option. This assessment is background to that plan.

Assessment: 2026-11-18. The initial host foundation is imported on
`codex/p25-integration`; radio firmware selection is unchanged. Nothing was
installed or tested on hardware.

| Baseline | Pinned revision |
|---|---|
| DM32Plus checkout | `79f660030d4b54935cbe57adb41b99d110e2956f` |
| Integration branch base, latest origin/main at branch creation | `ca28180dd4e21789f2627742359ff03221d8c924` |
| RadioFoundry upstream main fetched for this assessment | `c34dc440aaf2d3952458bc0bd85b8c56c76f6b23` |

## Decision

Use current RadioFoundry's MIT firmware as the implementation source for P25
Phase 1 and trunking. An independent trunking implementation is no
longer the first choice. Do not import the GPL fork or assume access to private
Open25 Next source.

Upstream has a conventional P25 stack, IMBE encode/decode, an air-interface HAL,
HR_C7000 glue, trunked system storage, radio-core integration, and a follower
that hunts control channels, follows selected voice grants, and returns to
control. Its feature matrix records DM-32UV bench checks of P25 receive and
transmit and trunked following. The trunking spec explicitly says its radio
checks used the project's own bench control channel, not a live system.

This is an integration and qualification project, not evidence that the fork
already supports P25 or that upstream works on the intended live system.

## Integration progress

The first import brings the generic air interface, FEC, fixed-point DSP, IMBE,
P25 conventional engines and trunking follower, with upstream host tests and
retained MIT/ISC notices. The production Makefile, feature selection, board,
radio state machine, product UI, site and release configuration are unchanged.
Upstream codeplug and modem-provider integration are not imported yet.

Eleven host test configurations passed using the workspace's Zig 0.14.1 C
compiler with `-Wall -Wextra -Wshadow -Werror`: FEC, framer, P25 units, the
engine with trunking off and on, voice decoder, encoder, trunking follower,
voice buffers, fixed-point kernels, and soft modem. The follower suite includes
the zero-send passive PTT case. A local test adaptation uses designated hook
initializers to eliminate warnings with trunking enabled.

Normal host builds run these through `tests/host/p25.mk` and `run-p25`. Windows
validation binaries and logs are local ignored output under `build/p25-host`.
The full existing host suite, target build, sanitizers, firmware matrix and
radio-level passive checks remain pending. Next: integrate the coordinated
board/air provider and codeplug contracts, then adapt product radio/UI/CPS paths.

## Requested behavior

Conventional and trunked Phase 1 receive and transmit, with the existing CPS
RX-only option respected. RX-only channels must not register, affiliate,
request a voice channel or transmit on PTT. A listen-only system independently
inhibits transmit. Channels and systems permitting transmit retain the upstream
active paths and normal admission checks. The tracker gives the permission
matrix and migration test requirements.

Start with C4FM Phase 1 control and voice channels within the board's usable
bands. Phase 2 voice, LSM/CQPSK, encryption and extra RF bands are outside this
integration's acceptance scope. One receiver misses control
signaling while following voice; quantify the practical effect.

Before hardware qualification, select representative test frequencies, control
and voice modulation, Phase 1 calls, site and talkgroups; no particular live
system is required by the owner. A
700/800 MHz-only target is outside the DM-32UV's coverage. Unsupported TDMA or
encrypted grants must be skipped rather than interpreted as clear Phase 1.

## What the comparison established

| Area | Current checkout | Pinned upstream | Integration implication |
|---|---|---|---|
| Radio modes | FM and DMR | P25 and NXDN paths also exist | Audit every mode switch and custom mode display. |
| Voice | AMBE-specific HAL | Separate full-rate IMBE/MBE modules | Carry the coordinated codec and scheduling changes. |
| Modem | DMR contract | `hal_air.h`, `hal_baseband.h`, shared air engines and HR_C7000 provider | Keep protocol, platform and board updates together. |
| Trunking | No P25 implementation | `core/p25/p25_follow.c` and signaling parser | Reuse follower behavior; independently qualify interoperability. |
| Programming | Older channel model | Mode registry, mode tags, RFKV records and P25 system codecs | Firmware and companion CPS must agree before enabling programming. |
| Board | Older audio/memory glue | Air provider wired on the board, revised audio, codec-table limit | Rebuild and measure on C-SKY; do not infer fit from ARM numbers. |
| Product | Custom home, controls, menus, talkgroups and deployment | Different UI and deployment content | Preserve and adapt product behavior explicitly. |

The two Git histories have no common ancestor; the checkout is not shallow.
The local history has 19 commits and begins with an imported initial snapshot.
The compared trees differ in 2,033 files (248,743 insertions, 37,202 deletions).
These counts include unrelated boards, generated assets, docs and site content;
they are not a count of required P25 changes. Do not use an unrelated-history
merge as an automatic update strategy.

Changes since the local initial snapshot touch 41 core files, 9 board files,
1 feature-config file, 27 test files, 49 site files, 20 tool files and 10 workflow
files. This captures post-import customizations only; the initial snapshot must
also be audited for product-specific differences.

## Receive-only controls verified by inspection

- `core/p25/p25_codeplug.c` decodes a system without `CODEPLUG_SYS_TX` as
  `listen_only`.
- `core/radio/radio_p25.c` adds `P25_FOLLOW_TX` only when the channel permits TX,
  the system is not listen-only, and the unit and talkgroup IDs are nonzero.
- `tests/host/p25TrunkTest.c` includes a listen-only case that presses PTT,
  advances time, and checks zero sends, registration off, request idle, and
  talk disabled. This test was inspected, not executed here.
- `P25_TSYS_NO_REG` / `P25_FOLLOW_NO_REG` is not a passive mode: it can request
  voice without registration. Never present it as receive-only in the CPS.

For passive operation configure the system as listen-only or its channels as
RX-only. Retain TX when both allow it. Test these controls through the real CPS
record decoder and radio state machine,
including restart, failed load, mode changes, retuning and PTT while following
voice. Measure zero RF emissions during the separately authorized bench session.
Upstream also implements active trunked transmit; merely enabling
`CONFIG_P25_TRUNK` does not make all configured systems passive.

## Recommended integration sequence

1. Create an isolated integration branch/worktree pinned to the revisions above.
   Keep a reproducible comparison against the product snapshot. No deployment or
   hardware actions belong in this step.
2. Establish a current upstream firmware baseline with its build scripts, tests,
   HAL, platform, board, storage and feature definitions. Preserve existing fork
   history and record imported revisions and notices. Avoid copying site content
   or replacing product/release configuration as a side effect.
3. Reapply product changes in small groups: identity and packaging; home and quick
   controls; menu/navigation; channels, zones, contacts and operating talkgroups;
   storage/data changes. Audit each against new modes and record formats. The
   local CI/release tools need explicit adaptation because upstream's build and
   test tooling has also changed.
4. Add P25 system/channel editing to the companion CPS against the exact
   imported record specification. Preserve FM/DMR and unknown records; do not
   initialize or erase on detection. Reject incompatible firmware explicitly.
5. Enable P25 and P25 trunking in the product preset and verify default and
   feature-off builds. Upstream `CONFIG_P25` currently needs `CONFIG_DMR` because
   it uses codeplug contacts. It derives IMBE, AIR and MBE dependencies. Decide
   whether NXDN belongs in this product separately; do not enable it incidentally.
6. Run host, storage, UI, C-SKY stack/ABI and firmware checks, then qualify
   control acquisition, voice following and allowed TX against independent
   equipment and representative site types. Record built, bench-tested and live-system-tested
   separately. Live reception alone does not establish all failure behavior.

Prefer carrying the coordinated upstream firmware over cherry-picking the P25
feature commit alone (`a3e9ee42`, #630). Subsequent commits fix editor mode
selection, dual-watch labeling, mixed scanning, and measured TX deviation.
The public Open25 timer/APRS conflict is not evidence of the current upstream
resource design; assess current upstream ownership and regression tests directly.

## Verification gates

| Gate | Required evidence |
|---|---|
| Provenance | Upstream per-file notices retained; IMBE third-party ISC attribution preserved; no GPL fork or private implementation imported. |
| Protocol and codec | `p25Test`, `p25EngineTest`, `p25VoiceTest`, `p25TrunkTest`, `mbeTest`, `imbeEncTest`, FEC and air tests pass. |
| Integration | `radioCoreTestP25`, `radioCoreTestP25T`, HR_C7000 air tests and P25 codeplug tests pass; passive cases exercise real record-to-follower configuration. |
| Product | Existing FM/DMR, APRS, custom talkgroups, storage, home/menu flows and companion CPS tests pass; translations synced and intended gallery images reviewed. |
| Build | Zero-warning C-SKY target build and stack/ABI checks; measured image/RAM budget; P25_TRUNK off, P25 off with dependencies, and affected switch matrix pass. |
| Hardware | Owner-authorized HIL; no passive signaling or PTT emissions; repeated acquisition, grants, end/loss, recovery and FM/DMR transitions; independent interoperability checks. |

No radio, debug link, SDR bench, service or CPS session was accessed. Any test
that transmits requires explicit authorization and the runner's --allow-rf gate.
Retain private codec/dump policies. Remove temporary CI/build trees after checks.

## Effort and remaining uncertainty

Withdraw the earlier independent-implementation schedule as the primary plan.
There is enough implementation upstream to justify a short integration spike:
first establish a reproducible build and the product customization map, then
estimate the remaining migration. A reliable delivery date needs that build,
CPS schema comparison and target-system information. The largest risks are
product/storage migration, live-system compatibility, resource headroom, and
proving passive operation through every path.

Assessment validation: upstream objects fetched without submodules; revisions,
ancestry, differences, configuration, selected implementation and tests inspected.
The foundation tests above were executed; no firmware build or full host suite
was performed. Python launchers work outside the sandbox, and the documentation
checker passed. Make/GCC are not on PATH and WSL is not installed. The existing
workspace portable Zig compiler works outside the sandbox. These environment
limits are not firmware test failures.

## Pinned evidence

- [P25 trunking specification](https://github.com/RadioFoundry/RadioFoundry/blob/c34dc440aaf2d3952458bc0bd85b8c56c76f6b23/docs/spec/p25-trunking.md)
- [P25 specification](https://github.com/RadioFoundry/RadioFoundry/blob/c34dc440aaf2d3952458bc0bd85b8c56c76f6b23/docs/spec/p25.md)
- [Feature matrix](https://github.com/RadioFoundry/RadioFoundry/blob/c34dc440aaf2d3952458bc0bd85b8c56c76f6b23/docs/feature-matrix.md)
- [Radio integration](https://github.com/RadioFoundry/RadioFoundry/blob/c34dc440aaf2d3952458bc0bd85b8c56c76f6b23/core/radio/radio_p25.c)
- [Follower tests](https://github.com/RadioFoundry/RadioFoundry/blob/c34dc440aaf2d3952458bc0bd85b8c56c76f6b23/tests/host/p25TrunkTest.c)
- [Local architecture](../dev/architecture.md), [local hardware](../spec/hardware-dm32.md), [local feature matrix](../feature-matrix.md).

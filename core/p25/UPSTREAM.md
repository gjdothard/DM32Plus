<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# P25 foundation provenance

Imported from [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry),
revision `c34dc440aaf2d3952458bc0bd85b8c56c76f6b23`, on 2026-11-18.
This is the project's MIT upstream, not Open25 Next or the GPL multimode fork.

The import includes:

- `core/air/`, `core/fec/`, `core/dsp/`, `core/mbe/`.
- `hal/hal_air.h` and `hal/hal_baseband.h`.
- `core/p25/` framing, receive/call engine, voice, signaling and follower sources;
  the initial import excluded the codeplug codec and radio-core integration.
- The host sources `fecTest.c`, `airFramerTest.c`, `p25Test.c`,
  `p25EngineTest.c`, `mbeTest.c`, `imbeEncTest.c`, `p25TrunkTest.c`,
  `p25VoiceTest.c`, `fxpTest.c`, and `airSoftmodemTest.c`.
- The follow-on provider import adds `drivers/modem/hrc7000/hrc7000_air.c`,
  its header, and `tests/host/hrc7000AirTest.c` from the same pinned revision.
  The existing DMR driver is retained.

Original notices are retained. `imbe_dec.c` and `imbe_tab.c` carry ISC notices
for mbelib's dequantizer and tables; the original notice text stays in those
files and the existing `LICENSES/ISC.txt` remains in the tree. Other imported
files identify MIT. No proprietary codec, stock image, recording, dump or key
is included.

The initial implementation import changed only Garrett Dothard's
copyright notice and line endings. Local `p25Voice.c` now honors the optional
audio-provider tail-drain hook on natural call completion; its host test covers
that change. The engine test's callback
tables use designated initializers so the same test builds without warnings
with trunking both enabled and disabled. Local host rules live
in `tests/host/p25.mk`; they add an engine build with trunking enabled as well
as the conventional build. Later local integration adds default-off protocol
switches and conditional firmware source selection in `config/p25.mk`, optional
board provider fields, and conditional IMBE About-page attribution. None of
these changes connects a working P25 radio channel. Hardware has not been
accessed during host validation.

The platform ownership wrappers in `platform/hrc7000/dmr_glue.c` are adapted
from the same upstream revision. Local changes suspend the scheduler during
queue handoff, refuse a lease while a DMR callback is attached, preserve the
existing DMR/BS logic, require explicit audio-gate callbacks for air TX, and
serialize gate release with deferred completion. The DMR byte queue is now
pointer-aligned with `_Alignas`; compile-time assertions check the reused
storage. The PA timer checks current TX activity before keying. Task wrappers
use the upstream `.flashfunc` mechanism in the linker. These adaptations are
covered by `modemGlueTest`. Target linking exposed an interrupt-RAM overflow;
local follow-on placement moves task-only DMR wrappers, deferred work and
diagnostics to flash while retaining IRQ callbacks and PA state reads in RAM.

Local PCM isolation work connects the board's air provider fields and its
audio-gate callbacks, centralizes ring-play requests, arbitrates APRS FIFO
ownership, and rejects stale callback output using a generation check and
separate scratch storage. Tests use the actual PCM/APRS worker and a modem
lifecycle linked to the real gate. This adapts the existing PCM driver; it does
not claim target qualification. Follow-on local scheduling reserves timer 6,
primes two PCM blocks and bounds each refill interrupt to one chunk, with a
nonblocking room-settling interval. This uses the pinned upstream's prompt-FIFO
settling observation; it does not copy its busy-wait wrapper. The optional audio
tail-drain hook prevents P25 and DMR natural completion from clipping queued
speech. Simulated timing and drain tests do not establish hardware behavior.

Future refreshes must compare against this exact revision, preserve the ISC
notices, and rerun the foundation tests. Imported test results are evidence of
host behavior only; upstream's RF bench qualification does not qualify this fork.

`p25_workspace.[ch]` is local integration code, not an upstream import. It groups
the imported state types and borrows idle codec state through optional HAL loan
hooks. The HR_C7000 adaptation lends its existing encoder-state buffer, excludes
codec calls while lent and reinitializes both codec states after return. It never
lends or changes the private codec's executable window or flash slot.

The initial follow-on `p25_codeplug.[ch]` import reused the parameter-only sections of
the pinned upstream: conventional, unit-ID, trunk-system, site, control-channel
and identifier codecs. Parameter function bodies are unchanged. The full loaders
and original header dependencies are now restored with the generic backend.
Conventional save and trunk fallback use the explicit tagged API. The alternative local record
format was discarded before commit. `p25CodeplugTest.c` isolates upstream's
vector test, skips unrelated name checks, uses standard C tokenization, and
checks 28/39 applicable rows. `tests/vectors/p25-codeplug.txt` contains the
unchanged P25 rows from upstream's `tests/vectors/channel-modes.txt`, with the
original MIT notice retained. The [integration spec](../../docs/spec/p25-codeplug.md)
records the backend dependencies and remaining RX-only admission work.

`core/codeplug/codeplug_kv.[ch]` and `tests/host/codeplugKvTest.c` are also from
the pinned upstream. The backend is initially selected with `CONFIG_P25`.
Local adaptations use the fork's existing storage-ready guard instead of the
unavailable upstream foreign-codeplug API, reject absent callbacks and wrapping
maps, refuse to format nonblank unknown contents anywhere in the sector, and
reject a NULL nonempty operation batch. Raw commit holds the recursive storage
lock across its keep decision and journal write. Local tests cover those guards
and an injected writer at the unlock boundary; removing the outer lock makes
that test lose the key. The full upstream power-cut/format/cursor suite remains.
The simulated-flash read counter is adapted from upstream for its walk-cost tests.
No transfer caller or channel tag is connected by this backend import alone.

`codeplug_tag.[ch]`, `codeplug_records.[ch]`, `codeplug_modes.h`, the full
`channel-modes.txt` vectors, `codeplugTagTest.c` and `p25StoredTest.c` also come from the same pin.
The stored test is upstream's full P25 codeplug test with standard C tokenization,
explicit tagged API calls and additional lifecycle/safe-base checks. Its future
runtime mode input uses a numeric cast until the P25 HAL owner is introduced.
The generic tag test uses explicit tagged APIs, checks absent channels through
the existing reader, and expects unavailable protocol owners to remain disabled.
The local registry keeps tagged kinds unrunnable and omits unavailable protocol
dependencies. Tag and record sources are initially gated by `CONFIG_P25`.
`codeplug_tagged.c` is a local adapter around upstream's lifecycle hooks; it
leaves the legacy radio reader unchanged, removes obsolete bindings before a
plain base write and preserves independent-base settings during tagged edits.
The base-name/copy helpers adapt the fork's existing storage layout. No radio
dispatch, UI or CPS caller is activated by these configuration APIs.

`p25_admission.[ch]` and `p25AdmissionTest.c` are local integration code. They
add strict stored-record permissions without changing upstream's tolerant
receive decoders. Tests exercise real stored records, per-read fault injection
and sampled journal recovery during channel lifecycle edits. The shared store's
local `codeplug_kv_lookup` distinguishes a missing optional key from an I/O
failure; existing `codeplug_kv_get` behavior stays unchanged. The radio owner
must still connect these permissions to actual emissions and cancel queued
work when settings change.

`core/radio/radio_p25.[ch]` adapts the pinned upstream's conventional stored
parameters, engine/voice startup, polling and transmit lifecycle. Local changes
use a sized exclusive codec loan, put the active channel and generation snapshot
inside that loan, omit upstream's RAM overrides and persistent diagnostic state,
and reject trunked/VFO startup at this stage. Stored TX permissions and band
checks guard prepare/go; teardown and abort unkey before returning memory.
The state-machine caller still owns keying and its normal RF protections.
The keyed store now supplies a full-width generation for session invalidation.

The shared `air_session_tx_go` locally aborts queued chunks when the provider
refuses its start. The new owner test reproduces that failure and verifies the
queue is empty before retry. `radioP25OwnerTest.c` is local integration code
using real engines/voice with simulated hardware.

The subsequent local state-machine integration connects stored conventional
P25 selection, PTT and the existing RF safeguards to that owner. It adds no
upstream radio firmware dependency. Shared tag/store readers now remain built
with P25 off to park unsupported records. The legacy raw reader stays available
for unaware configuration clients; the guarded radio reader is separate.
Legacy CPS/fast-CPS/TFTP writes refuse tagged codeplugs and cannot overwrite
managed metadata. Mode-aware editing and its synchronous quiescence contract
remain pending. State-machine tests use the real stored records, DMR stack and
owner with simulated RF/PCM/air hardware. The home mode label, mic gain and beep
mixer follow existing product paths.

The trunked owner now adapts this same pinned implementation's follower
configuration, provider/radio-task exclusion, deferred retunes and subscriber
PTT lifecycle. Configuration arrays and pending work join the existing loan;
the stored-loader output shares voice scratch only before voice startup.
Local safeguards check strict permissions and normal radio admission for each
actual frequency, bind queued bursts to their retune generation, clear failed
starts, and reject stale grants or invalid retunes. Voice callbacks are gated
to a confirmed followed channel. No upstream RAM overrides, setting overrides
or permanent diagnostic snapshots are imported. Encrypted calls are always
skipped; fallback activation and mode-aware UI/CPS remain pending.

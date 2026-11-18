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

The follow-on `p25_codeplug.[ch]` import reuses the parameter-only sections of
the pinned upstream: conventional, unit-ID, trunk-system, site, control-channel
and identifier codecs. Function bodies are unchanged. Storage/tag/system loaders
remain deferred until their generic backend is ported; the header includes
`config.h` instead of the absent backend model. The alternative local record
format was discarded before commit. `p25CodeplugTest.c` isolates upstream's
vector test, skips unrelated name checks, uses standard C tokenization, and
checks 28/39 applicable rows. `tests/vectors/p25-codeplug.txt` contains the
unchanged P25 rows from upstream's `tests/vectors/channel-modes.txt`, with the
original MIT notice retained. The [integration spec](../../docs/spec/p25-codeplug.md)
records the backend dependencies and safe stand-in/RX-only adaptation still needed.

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

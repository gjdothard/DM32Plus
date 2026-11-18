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
  it excludes upstream's codeplug codec and all radio-core integration.
- The host sources `fecTest.c`, `airFramerTest.c`, `p25Test.c`,
  `p25EngineTest.c`, `mbeTest.c`, `imbeEncTest.c`, `p25TrunkTest.c`,
  `p25VoiceTest.c`, `fxpTest.c`, and `airSoftmodemTest.c`.

Original notices are retained. `imbe_dec.c` and `imbe_tab.c` carry ISC notices
for mbelib's dequantizer and tables; the original notice text stays in those
files and the existing `LICENSES/ISC.txt` remains in the tree. Other imported
files identify MIT. No proprietary codec, stock image, recording, dump or key
is included.

Imported implementation is unchanged apart from adding Garrett Dothard's
copyright notice and normalizing line endings. The engine test's callback
tables use designated initializers so the same test builds without warnings
with trunking both enabled and disabled. Local host rules live
in `tests/host/p25.mk`; they add an engine build with trunking enabled as well
as the conventional build. Production build selection is unchanged.

Future refreshes must compare against this exact revision, preserve the ISC
notices, and rerun the foundation tests. Imported test results are evidence of
host behavior only; upstream's RF bench qualification does not qualify this fork.

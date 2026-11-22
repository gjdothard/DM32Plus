<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Clean-room development

RadioFoundry and this fork are MIT licensed. The contributor rules in [AGENTS.md](../AGENTS.md) apply
to code, tools, tests, documentation, and release artifacts.

## Sources

Work from the prose specifications in [docs/spec](spec/README.md), datasheets, public standards such
as ETSI TS 102 361 and the MMDVM protocol, and permissively licensed code. Preserve the license and
copyright notices of permitted dependencies.

Do not open, read, search, or copy another radio firmware's source, OpenRTX, MMDVMHost, GPL code,
restrictively licensed vendor SDKs, or disassembly. A link or acknowledgment is not permission to
inspect those sources. If a task appears to require them, stop and ask the project owner.

## Private radio data and release contents

Never commit the AMBE codec, stock firmware images, bootloader keys, memory or flash dumps, or audio
recordings. Keep the `codecs/` and `build/` ignore rules intact. Release images exclude the codec;
the user's installer obtains it locally as described in [Installing](user/02-installing.md).

Use synthetic test fixtures and example data in documentation screenshots. Keep hardware observations
in the radio's bring-up record and distinguish them from host tests and inferred behavior.

Follow [Contributing](dev/contributing.md) and the [release checklist](dev/release.md#checklist) for
review and validation. These rules describe this project's accepted development sources and artifacts.

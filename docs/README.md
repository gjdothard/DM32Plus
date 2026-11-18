<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Documentation

Start with [product setup and release channels](user/23-radios.md) or
[CPS programming](user/04-programming.md). These docs retain the shared RadioFoundry architecture and
board references; this fork's release scope is described in [CI](dev/ci.md).

| Directory or page | For | Contents |
|---|---|---|
| [user/](user/README.md) | Users | The user manual: installing, keys and screens, FM, DMR, hotspot, settings, the radio reference. Built to PDF on release tags |
| [dev/](dev/README.md) | Contributors | Architecture, building, testing, CI, adding a radio, the codec, releases, the web site |
| [spec/](spec/README.md) | Contributors | Behavior, protocols, the DMR modems, each radio's hardware |
| [ux/](ux/) | Contributors | UI design and the [current firmware screenshots](ux/screens/README.md) |
| [testing/](testing/README.md) | Contributors | Every test tier, on-air coverage, bench procedures, bring-up checklists |
| [build-config.md](build-config.md) | Contributors | Every build switch and value, variants, board defaults |
| [feature-matrix.md](feature-matrix.md) | Contributors | Each feature's flash cost and per-radio default; whole-image sizes |
| [CLEANROOM.md](CLEANROOM.md) | Contributors | The clean-room rules |

## Support

Ask questions and report problems in [this project's GitHub issues](https://github.com/gjdothard/DM32Plus/issues).
Include your firmware version, steps to reproduce the problem, and any error report downloaded from CPS or the installer.

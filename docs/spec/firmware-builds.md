<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Custom firmware build service

The browser selects a published release version and a complete enabled-feature list. The product metadata supplies
the repository, board, required programming/recovery features, build proxy path, and allowed public browser origins.
No request supplies a repository URL, branch, executable, compiler flag, or arbitrary Make variable.

## Protocol

The same-origin proxy is `/_firmware_build/v1/`. The internal service uses `/v1/` with the same suffixes.

| Method and suffix | Request or response |
|---|---|
| `POST builds` | JSON: `board`, `ref` (release version), `testing` (legacy RC tag), `features` (unique enabled names), `options` (empty) |
| `GET builds/<id>` | JSON: `id`, `status`; queued jobs include `position`, failed jobs include `error` |
| `GET builds/<id>/firmware.json` | Manifest of a completed job |
| `GET builds/<id>/firmware.bin` | Codec-free image of a completed job |
| `GET health` | Service status |

Statuses are `queued`, `running`, `done`, and `failed`. A done response includes relative `manifest` and `image` paths.
The browser polls until completion, stops consuming a result after the selected version or features change, and
checks the manifest and image before proceeding to the write steps. Failed or incomplete jobs expose no artifact.

Requests are at most 32 KiB. The proxy accepts POST only from a same-origin browser or an explicitly configured product origin.
It forwards only the routes above to its operator-configured backend. Artifacts are limited to 2 MiB in the proxy.
Error reporting, stock images, codeplugs, and encryption keys are not part of this protocol.

## Build and identity

The worker reads the site's feature catalog, refreshed each minute. It rejects unsupported, not-written, unknown,
duplicate, and dependency-invalid choices, and refuses to disable the product's required connection features.
It fetches only a release tag in the product repository and the pinned permissively licensed RTOS submodule.
Each job has a fresh source workspace. Explicit `CONFIG_` values are checked with `make print-config` before compiling.
The build must be warning-free and pass the stack check. The release tool verifies application ranges and codec exclusion.

Custom manifests retain the release version, exact `source_tag`, and actual source commit, use `variant=custom`, and include
`selected_features` (sorted), the resolved `features`, and the `off` list. The image metadata gives its size and SHA-256.
The browser additionally checks product identity, version, codec exclusion, and exact selected/resolved feature lists.
The firmware reports a custom preset because the build uses explicit `CONFIG_` arguments.

## Scheduling and cache

One worker runs at a time; four jobs may wait. Six new selections per minute are accepted globally. Identical requests
share an active job or a verified cached result. Cache identity includes repository, compiler image identity, board,
release version, and feature selection. Release tags are immutable; changing published tags is unsupported.
Sixteen completed/failed result directories are retained, excluding active jobs. A restart verifies cached artifact
identity and hashes before reuse. Failed requests can be retried. Workspaces are removed after each attempt.

The worker runs without root, a Docker socket, or host ports. Its code and root filesystem are read-only.
The installed service limits CPU, memory, processes, and temporary space. A build has a 15-minute command deadline.
See [host setup](../../tools/firmware_builder/README.md) for installation and rollback.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Release distribution

The version grammar and compatibility ordering are defined in
[Release names](../dev/release.md#release-names). Release manifests bind the product, board, full variant,
version, commit and image checksum. Firmware releases exclude the proprietary voice codec.

Development and Testing use separate prerelease counters. Stable tags are explicit release actions.
Installers preserve the historical calendar tags and route new semantic tags without a product prefix.
The local release proxy validates the version before constructing a download URL in the product's repository.

After a stable firmware release is published, its website job builds from the same commit and firmware
artifact. Packaging rejects an incorrect product, version, board, variant, codec status or image checksum.
The bundle includes the portable server, shared version parser, product metadata and Compose definition.
Every bundled file has a SHA-256 checksum.

The website host checks the public latest stable release using outbound HTTPS. It deploys only a newer
semantic final with a complete website asset and a matching GitHub asset digest. Drafts, prereleases,
missing bundles and downgrades leave the current website running.

Deployment verifies checksums before replacing files. It serializes updates, preserves local proxy and port
configuration, retains a rollback copy, and recreates the Compose service. A failed container health check
restores the previous site, server files. The public `deployment.json` marker appears only after
the container passes its health check. It identifies both the release version and firmware commit.
The release workflow requires this exact marker before reporting website deployment success.

Host regression coverage is in `releaseVersionTest.py` and `websiteReleaseTest.py`. Installer channel and proxy
tests are in `site/tests/release-channels.test.mjs`, `version.test.mjs` and `offline_cps_test.py`.
See [website deployment](../dev/website.md#website-deployment-after-a-release) for setup and operations.

The polling account has no login, Docker group membership or general sudo access. A root-owned helper accepts
no command-line arguments and verifies the official release independently. Its root-owned Compose definition,
project, image, ports and mounts cannot be changed by the account or a downloaded bundle. Archive extraction
rejects paths, links, types and permissions that could escape the controlled deployment directory.

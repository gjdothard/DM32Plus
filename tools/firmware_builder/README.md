<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# On-demand firmware builds

The CPS feature picker submits a release version and enabled features to the same-origin build proxy.
The proxy forwards only build requests, status, and firmware artifacts to this internal service.
It never forwards codeplugs, stock firmware, codecs, or encryption keys.

The worker fetches release tags from the product's configured repository. It accepts no client-supplied repository,
branch, compiler command, or make variable. It checks feature names, dependencies, unavailable features, and required
programming/recovery features. Make validates the selection again against the selected release.
Successful images must compile without warnings, pass the stack check, and pass the release tool's codec/range checks.
The browser verifies the artifact hash, product, version, and exact enabled-feature list before flashing.

One worker processes a queue of at most four waiting jobs. New selections are limited to six per minute globally;
duplicates share a job. Sixteen result directories are retained. Cache identity includes the compiler, repository,
release version, and feature selection. Cache hits revalidate the image checksum and manifest after a restart.
Failed jobs return an error and never expose an image download. Compile workspaces are removed after each attempt.

## Host setup

Use the target platform's compiler container, pinned by digest. Install the worker without a Docker socket,
host ports, supplementary groups, or a privileged user. The root filesystem and installed code are read-only.
The installer sets one CPU, 1200 MiB RAM, 1500 MiB total RAM/swap, and 96 processes.
It preserves the existing production CPS service's bindings and security settings.

From a verified checkout or staged bundle, an administrator runs:

```sh
sudo sh tools/firmware_builder/install.sh \
    /opt/DEPLOYMENT boards/BOARD/product.json \
    REGISTRY/COMPILER@sha256:DIGEST https://PUBLIC_SITE/
```

`tools/release_version.py` must be beside the `firmware_builder/` directory. The product's `build_api` selects the
same-origin route, `build_origins` permits the site's browser origin, and `build_required` locks the programming
and recovery features. The server needs `CPS_BUILD_API=http://firmware-builder:8081/v1/`.
The installer also sets `CPS_BUILD_ORIGINS` to the public HTTPS origin so a reverse proxy's internal Host header
does not block valid browser requests. Deploy the current website server files before running the installer.
`tools/website/serve.py` passes that setting to the proxy. Portable/offline servers without a builder return an
unavailable message for custom builds; bundled Full firmware remains usable offline.

The catalog is read from the public site's `data/radios.json` and `data/features.json` and refreshed each minute.
The selected release's own Makefile remains authoritative. A newer site's feature missing from an older release
produces an explicit error rather than silently ignoring the option.

Inspect `docker compose -f /etc/website-updater/compose.yaml logs firmware-builder` and the build cache under
`/var/lib/firmware-builder`. Failed build logs remain with their result directory. The installer saves the previous
Compose definition and restores it if startup fails. Restoring that definition and recreating CPS removes the proxy
connection; retain the cache until no build is running.

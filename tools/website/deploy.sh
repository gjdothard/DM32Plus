#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# Run on the website host: deploy.sh ROOT ARCHIVE SHA256 VERSION
set -eu
root=$(realpath "$1")
archive=$(realpath "$2")
case $root in /|'') echo "invalid deployment root" >&2; exit 2 ;; esac
printf '%s  %s\n' "$3" "$archive" | sha256sum -c -
cd "$root"
# Serialize deployments with manual updates using the same lock.
exec 9> .deploy.lock
flock -w 120 9
candidate=$(mktemp -d "$root/.release-next.XXXXXXXX")
cleanup() {
    # Only remove this invocation's mktemp directory, never a deployed or backup tree.
    python3 - "$root" "$candidate" <<'PY'
from pathlib import Path
import shutil, sys
root, candidate = (Path(p).resolve() for p in sys.argv[1:])
if candidate.parent != root or not candidate.name.startswith(".release-next."):
    raise SystemExit("invalid temporary deployment directory")
shutil.rmtree(candidate)
PY
}
trap 'cleanup' EXIT
tar -xzf "$archive" --no-same-owner -C "$candidate"
chmod 755 "$candidate"
for path in site/codeplug/index.html site/index.html site/LICENSE server.py serve.py release_version.py release-products.json compose.yaml; do
    test -s "$candidate/$path"
done
(cd "$candidate" && sha256sum -c SHA256SUMS)
python3 - "$candidate/site/deployment.json" "$4" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    marker = json.load(f)
if marker.get("version") != sys.argv[2]:
    raise SystemExit("website bundle does not match the requested release")
PY
# The public marker confirms a completed health check, not merely files on disk.
mv "$candidate/site/deployment.json" "$candidate/deployment.pending.json"
backup=$(mktemp -d "$root/release.previous.XXXXXXXX")
files="site server.py serve.py release_version.py release-products.json"
if [ -z "${WEBSITE_COMPOSE_FILE:-}" ]; then files="$files compose.yaml"; fi
compose() {
    if [ -n "${WEBSITE_COMPOSE_FILE:-}" ]; then
        docker compose -f "$WEBSITE_COMPOSE_FILE" "$@"
    else
        docker compose "$@"
    fi
}
rollback() {
    for path in $files; do
        if [ -e "$backup/$path" ]; then
            if [ ! -e "$candidate/$path" ] && [ -e "$root/$path" ]; then mv "$root/$path" "$candidate/$path"; fi
            mv "$backup/$path" "$root/$path"
        elif [ ! -e "$candidate/$path" ] && [ -e "$root/$path" ]; then
            mv "$root/$path" "$candidate/$path"
        fi
    done
    compose up -d --force-recreate --wait --wait-timeout 60 cps
}
committed=0
trap 'if [ "$committed" = 0 ]; then rollback; fi; cleanup' EXIT
trap 'exit 1' HUP INT TERM
for path in $files; do
    if [ -e "$path" ]; then mv "$path" "$backup/$path"; fi
    mv "$candidate/$path" "$path"
done
if ! compose up -d --force-recreate --wait --wait-timeout 60 cps; then
    exit 1
fi
mv "$candidate/deployment.pending.json" "$root/site/deployment.json"
committed=1
trap - HUP INT TERM
compose ps cps
printf 'Previous website retained at %s\n' "$backup"

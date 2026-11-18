#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Fixed deployment operation for the unprivileged website updater account."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

# Installed code and configuration are owned by root and not writable by the updater.
sys.path.insert(0, "/usr/local/lib/website-updater")
import update as updater


def validate_archive(archive):
    allowed = {"server.py", "serve.py", "release_version.py", "release-products.json", "compose.yaml", "SHA256SUMS"}
    seen, total = set(), 0
    with tarfile.open(archive) as bundle:
        for member in bundle:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or ".." in path.parts or not path.parts
                    or (path.parts[0] != "site" and str(path) not in allowed)
                    or not (member.isfile() or member.isdir())
                    or member.mode & 0o7022 or str(path) in seen):
                raise ValueError("unsafe website archive member: " + member.name)
            total += member.size
            if total > 1024 * 1024 * 1024:
                raise ValueError("website expands beyond size limit")
            seen.add(str(path))


def deploy(stream, config, work):
    header = stream.readline(4097)
    if len(header) > 4096 or not header.endswith(b"\n"):
        raise ValueError("invalid deployment request")
    version = json.loads(header).get("version")
    repo = config["repository"]
    url = f"https://api.github.com/repos/{repo}/releases/latest"
    with urllib.request.urlopen(updater.request(url), timeout=30) as response:
        release = json.load(response)
    root = Path(config["deployment_root"])
    marker = root / "site/deployment.json"
    current = json.loads(marker.read_text()).get("version") if marker.is_file() else None
    selected = updater.selected_asset(release, current)
    if not selected or selected[0] != version:
        raise ValueError("only the repository's newer stable website release can deploy")
    checksum, size = hashlib.sha256(), 0
    with tempfile.TemporaryDirectory(prefix="release.", dir=work) as tmp:
        archive = Path(tmp) / "website.tar.gz"
        with archive.open("wb") as output:
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                if size > 256 * 1024 * 1024:
                    raise ValueError("website archive exceeds size limit")
                checksum.update(chunk)
                output.write(chunk)
        if checksum.hexdigest() != selected[2]:
            raise ValueError("website archive does not match GitHub's digest")
        validate_archive(archive)
        # The archive's Compose file is never used by the privileged Docker command.
        env = {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "HOME": "/var/lib/website-deployer",
               "WEBSITE_COMPOSE_FILE": "/etc/website-updater/compose.yaml"}
        subprocess.run(["/bin/sh", "/usr/local/lib/website-updater/deploy.sh", str(root),
                        str(archive), selected[2], version], env=env, check=True)


if __name__ == "__main__":
    if os.geteuid() != 0 or len(sys.argv) != 1:
        raise SystemExit("the deployment helper requires root and accepts no arguments")
    config = json.loads(Path("/etc/website-updater/config.json").read_text())
    deploy(sys.stdin.buffer, config, Path("/var/lib/website-deployer"))

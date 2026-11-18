#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Fetch and deploy a newer stable website bundle from the configured public repository."""
import hashlib
import json
from pathlib import Path
import re
import socket
import tempfile
import urllib.error
import urllib.request

import release_version


def selected_asset(release, current=None):
    version = release.get("tag_name")
    key = release_version.parse(version)
    if (key is None or key[0] != 1 or key[4] != 2 or release.get("draft") or release.get("prerelease")):
        return None
    if current and (previous := release_version.parse(current)) and key <= previous:
        return None
    name = "website-" + version + ".tar.gz"
    for asset in release.get("assets", []):
        digest = re.fullmatch(r"sha256:([a-f0-9]{64})", asset.get("digest", ""))
        if asset.get("name") == name and asset.get("state") == "uploaded" and digest:
            return version, name, digest[1]
    return None


def request(url):
    return urllib.request.Request(url, headers={"User-Agent": "website-release-updater", "Accept": "application/vnd.github+json"})


def send_bundle(archive, version, address="/run/website-updater/deploy.sock"):
    """Send bytes to the fixed root service without changing this process's privileges."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(240)
        connection.connect(str(address))
        connection.sendall((json.dumps({"version": version}) + "\n").encode())
        with archive.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                connection.sendall(chunk)
        connection.shutdown(socket.SHUT_WR)
        with connection.makefile("rb") as response:
            line = response.readline(4097)
        if len(line) > 4096 or not line.endswith(b"\n"):
            raise ValueError("invalid deployment helper response")
        result = json.loads(line)
        if result.get("ok") is not True:
            raise ValueError("deployment helper failed: " + str(result.get("error", "unknown error")))


def update(root, config_file=None):
    import fcntl
    config = json.loads((config_file or root / "release-update.json").read_text(encoding="utf-8"))
    repo = config["repository"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("invalid repository")
    with (root / ".release-update.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            with urllib.request.urlopen(request(f"https://api.github.com/repos/{repo}/releases/latest"), timeout=30) as response:
                release = json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return
            raise
        marker = Path(config["deployment_root"]) / "site/deployment.json"
        current = json.loads(marker.read_text(encoding="utf-8")).get("version") if marker.is_file() else None
        selected = selected_asset(release, current)
        if not selected:
            return
        version, name, digest = selected
        url = f"https://github.com/{repo}/releases/download/{version}/{name}"
        with tempfile.TemporaryDirectory(prefix=".release-download.", dir=root) as tmp:
            archive = Path(tmp) / name
            checksum = hashlib.sha256()
            size = 0
            with urllib.request.urlopen(request(url), timeout=60) as response, archive.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    size += len(chunk)
                    if size > 256 * 1024 * 1024:
                        raise ValueError("website archive exceeds size limit")
                    checksum.update(chunk)
                    output.write(chunk)
            if checksum.hexdigest() != digest:
                raise ValueError("website archive does not match GitHub's digest")
            send_bundle(archive, version)
            deployed = json.loads(marker.read_text(encoding="utf-8"))
            if deployed.get("version") != version:
                raise ValueError("deployed website version differs from release")
            print("Deployed", version, flush=True)


if __name__ == "__main__":
    update(Path("/var/lib/website-updater"), Path("/etc/website-updater/config.json"))

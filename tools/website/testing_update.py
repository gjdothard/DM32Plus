#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Deploy verified website bundles from successful Testing firmware runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile
import urllib.request


def request_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "testing-website-updater",
                                                   "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def completed_run(runs):
    return next((r for r in runs if r.get("head_branch") == "Testing" and r.get("event") == "push"
                 and r.get("status") == "completed" and r.get("conclusion") == "success"
                 and re.fullmatch(r"[0-9a-f]{40}", r.get("head_sha", ""))), None)


def selected_asset(releases):
    candidates = []
    for release in releases:
        tag = release.get("tag_name", "")
        match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)-rc\.(\d+)", tag)
        if not match or release.get("draft") or not release.get("prerelease"):
            continue
        for asset in release.get("assets", []):
            digest = re.fullmatch(r"sha256:([0-9a-f]{64})", asset.get("digest", ""))
            if asset.get("name") == f"website-testing-{tag}.tar.gz" and asset.get("state") == "uploaded" and digest:
                candidates.append((tuple(map(int, match.groups())), tag, digest[1]))
    return sorted(candidates, reverse=True)


def unpack(archive, destination, sha, version):
    """Validate all paths and checksums before any extracted file can be served."""
    allowed = {"server.py", "serve.py", "release_version.py", "release-products.json", "compose.yaml", "SHA256SUMS"}
    with tarfile.open(archive) as bundle:
        seen, total = set(), 0
        for member in bundle.getmembers():
            path = PurePosixPath(member.name)
            name = str(path)
            if (path.is_absolute() or ".." in path.parts or not path.parts
                    or (path.parts[0] != "site" and name not in allowed)
                    or not (member.isfile() or member.isdir()) or member.mode & 0o7022 or name in seen):
                raise ValueError("unsafe website archive member: " + member.name)
            total += member.size
            if total > 1024 * 1024 * 1024:
                raise ValueError("website archive expands beyond size limit")
            seen.add(name)
        # Every member was checked above: no links, traversal, devices or writable modes.
        # The deployment host can run Python versions predating tarfile's filter argument.
        bundle.extractall(destination)
    expected = {}
    for line in (destination / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or name in expected or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("invalid checksum entry")
        expected[name] = digest
    actual = {p.relative_to(destination).as_posix() for p in destination.rglob("*") if p.is_file()
              and p.relative_to(destination).as_posix() != "SHA256SUMS"}
    if set(expected) != actual:
        raise ValueError("incomplete website checksums")
    for name, digest in expected.items():
        if hashlib.sha256((destination / name).read_bytes()).hexdigest() != digest:
            raise ValueError("website checksum mismatch: " + name)
    marker = json.loads((destination / "site/deployment.json").read_text())
    if marker.get("channel") != "Testing" or marker.get("git") != sha or marker.get("site_git") != sha or marker.get("version") != version:
        raise ValueError("website does not match the successful Testing run")
    for name in ("site/codeplug/index.html", "site/index.html", "site/LICENSE", "server.py", "serve.py", "release_version.py", "release-products.json"):
        if not (destination / name).is_file():
            raise ValueError("incomplete website bundle: " + name)


def compose(config):
    subprocess.run(["docker", "compose", "-p", "dm32plus-cps-testing", "-f", config["compose"],
                    "up", "-d", "--no-deps", "--force-recreate", "--wait", "--wait-timeout", "60", "testing-cps"],
                   check=True, timeout=150)


def activate(root, release, config, sha):
    current, next_link = root / "current", root / ".current-next"
    previous = current.readlink() if current.is_symlink() else None
    if current.exists() and not current.is_symlink():
        raise ValueError("current deployment must be a symlink")
    next_link.unlink(missing_ok=True)
    next_link.symlink_to(release, target_is_directory=True)
    next_link.replace(current)
    try:
        compose(config)
        marker = request_json(config["health_url"])
        if marker.get("git") != sha or marker.get("channel") != "Testing":
            raise ValueError("served website marker does not match the candidate")
    except Exception:
        if previous is not None:
            next_link.symlink_to(previous, target_is_directory=True)
            next_link.replace(current)
            compose(config)
        else:
            current.unlink()
        raise


def update(config):
    import fcntl
    root = Path(config["root"]).resolve()
    repository = config["repository"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("invalid repository")
    with (root / ".testing-update.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        api = f"https://api.github.com/repos/{repository}"
        runs = request_json(api + "/actions/workflows/product-firmware.yml/runs?branch=Testing&per_page=5")["workflow_runs"]
        run = completed_run(runs)
        if not run:
            return
        marker_path = root / "current/site/deployment.json"
        current = json.loads(marker_path.read_text()) if marker_path.is_file() else {}
        if current.get("git") == run["head_sha"] or current.get("testing_run_id", 0) >= run["id"]:
            return
        candidates = selected_asset(request_json(api + "/releases?per_page=20"))
        for _, version, digest in candidates:
            # Pin both the completed run and the release's immutable source tag.
            commit = request_json(api + "/commits/" + version)
            if commit.get("sha") != run["head_sha"]:
                continue
            with tempfile.TemporaryDirectory(prefix=".testing-download.", dir=root) as temp:
                archive = Path(temp) / "website.tar.gz"
                url = f"https://github.com/{repository}/releases/download/{version}/website-testing-{version}.tar.gz"
                with urllib.request.urlopen(url, timeout=60) as response, archive.open("wb") as output:
                    size, checksum = 0, hashlib.sha256()
                    while chunk := response.read(1024 * 1024):
                        size += len(chunk)
                        if size > 256 * 1024 * 1024:
                            raise ValueError("website archive exceeds size limit")
                        output.write(chunk)
                        checksum.update(chunk)
                if checksum.hexdigest() != digest:
                    raise ValueError("website archive does not match GitHub's digest")
                release = root / "releases" / f"auto-{run['id']}-{run['head_sha'][:8]}"
                release.mkdir(parents=True)
                # A failed attempt is retained. A fresh directory makes retries unambiguous.
                candidate = Path(tempfile.mkdtemp(prefix="candidate-", dir=release))
                os.chmod(candidate, 0o755)
                unpack(archive, candidate, run["head_sha"], version)
                path = candidate / "site/deployment.json"
                marker = json.loads(path.read_text())
                marker["testing_run_id"] = run["id"]
                path.write_text(json.dumps(marker) + "\n")
                activate(root, candidate, config, run["head_sha"])
                print(f"Testing deployed: {version} {run['head_sha']}", flush=True)
            return


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    update(json.loads(args.config.read_text()))

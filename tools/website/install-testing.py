#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Install an independent Testing updater using the deployment owner's crontab."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess


def install(root, repository):
    root = root.resolve(strict=True)
    if (root == Path("/") or not re.fullmatch(r"/[A-Za-z0-9_./-]+", str(root))
            or not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository)):
        raise ValueError("invalid Testing deployment configuration")
    source = root / "compose.yaml"
    compose = json.loads(subprocess.check_output(["docker", "compose", "-p", "dm32plus-cps-testing",
                       "-f", str(source), "config", "--format", "json"], text=True))
    if set(compose["services"]) != {"testing-cps"}:
        raise ValueError("expected the separate Testing service only")
    service = compose["services"]["testing-cps"]
    paths = []
    names = {"site", "serve.py", "server.py", "release_version.py", "release-products.json"}
    for volume in service["volumes"]:
        old = Path(volume["source"])
        if volume.get("type") != "bind" or old.name not in names or not old.resolve().is_relative_to(root):
            raise ValueError("unexpected Testing volume")
        paths.append(old.parent)
        volume["source"] = str(root / "current" / old.name)
    if len(set(paths)) != 1 or {Path(v["source"]).name for v in service["volumes"]} != names:
        raise ValueError("Testing mounts must share a release directory")
    current = root / "current"
    if not current.exists():
        current.symlink_to(paths[0], target_is_directory=True)
    if not current.is_symlink():
        raise ValueError("current must be a symlink")
    control = root / "updater"
    control.mkdir(exist_ok=True)
    backup = control / "compose.before-automatic.yaml"
    if not backup.exists():
        shutil.copy2(source, backup)
    shutil.copy2(Path(__file__).with_name("testing_update.py"), control / "testing_update.py")
    fixed_compose = control / "compose.json"
    fixed_compose.write_text(json.dumps(compose, indent=2) + "\n")
    subprocess.run(["docker", "compose", "-f", str(fixed_compose), "config", "--quiet"], check=True)
    config = control / "config.json"
    config.write_text(json.dumps(dict(root=str(root), repository=repository, compose=str(fixed_compose),
                      health_url="http://127.0.0.1:8083/deployment.json"), indent=2) + "\n")
    # Preserve every unrelated cron entry. No privileged service or production configuration changes.
    existing = subprocess.run(["crontab", "-l"], capture_output=True, text=True)
    if existing.returncode and "no crontab" not in existing.stderr:
        raise RuntimeError(existing.stderr)
    label = "# testing-cps-website-updater"
    lines = [line for line in existing.stdout.splitlines() if label not in line]
    lines.append(f"*/2 * * * * /usr/bin/python3 {control}/testing_update.py --config {config} >> {control}/update.log 2>&1 {label}")
    subprocess.run(["crontab", "-"], input="\n".join(lines) + "\n", text=True, check=True)
    print("Testing updater installed; polling every two minutes. Production is unchanged.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    args = parser.parse_args()
    install(args.root, args.repository)

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Bundle a rendered website and its matching release proxy for Compose deployment."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def package(site, product_file, version, output):
    product = json.loads(product_file.read_text(encoding="utf-8"))
    base = f"{product['prefix']}-{product['board']}-{version}"
    manifest_path = site / "firmware" / product["board"] / (base + ".json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    image = manifest_path.with_suffix(".bin")
    if (manifest.get("product") != product["name"] or manifest.get("version") != version
            or manifest.get("board") != product["board"] or manifest.get("variant") != "full"
            or manifest.get("codec", {}).get("included") is not False
            or manifest.get("image", {}).get("file") != image.name
            or not re.fullmatch(r"[0-9a-f]{40}", manifest.get("git_full", ""))
            or hashlib.sha256(image.read_bytes()).hexdigest() != manifest["image"]["sha256"]):
        raise ValueError("website firmware does not match the release")
    for path in site.rglob("*"):
        if path.is_symlink():
            raise ValueError("website must not contain symlinks")
    for name in ("codeplug/index.html", "index.html", "LICENSE"):
        if not (site / name).is_file():
            raise ValueError("incomplete website: " + name)
    with tempfile.TemporaryDirectory() as tmp:
        bundle = Path(tmp)
        shutil.copytree(site, bundle / "site")
        (bundle / "site/deployment.json").write_text(json.dumps({
            "_license": "SPDX-License-Identifier: MIT",
            "_copyright": "Copyright (c) 2026 Garrett Dothard",
            "version": version, "git": manifest["git_full"], "product": product["name"],
        }) + "\n", encoding="utf-8")
        for name in ("serve.py", "compose.yaml"):
            shutil.copyfile(ROOT / "tools/website" / name, bundle / name)
        shutil.copyfile(ROOT / "tools/offline_cps/server.py", bundle / "server.py")
        shutil.copyfile(ROOT / "tools/release_version.py", bundle / "release_version.py")
        (bundle / "release-products.json").write_text(json.dumps([product]) + "\n", encoding="utf-8")
        files = sorted(p for p in bundle.rglob("*") if p.is_file())
        sums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(bundle).as_posix()}\n" for p in files)
        (bundle / "SHA256SUMS").write_text(sums, encoding="utf-8")
        output.parent.mkdir(parents=True, exist_ok=True)
        def metadata(info):
            info.mode = 0o755 if info.isdir() else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            return info
        with tarfile.open(output, "w:gz") as archive:
            for path in sorted(bundle.iterdir()):
                archive.add(path, arcname=path.name, filter=metadata)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--site", type=Path, required=True)
    ap.add_argument("--product", type=Path, required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    package(args.site, args.product, args.version, args.out)

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Package an already-built site with a verified Windows embeddable Python ZIP. No network access."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile


def sha256(path):
	with path.open("rb") as f:
		return hashlib.file_digest(f, "sha256").hexdigest()


def package(site, runtime_zip, runtime_info, out):
	for name in ("codeplug/index.html", "data/radios.json", "static/js/editor/app.js"):
		if not (site / name).is_file():
			raise ValueError("incomplete site: " + name)
	info = json.loads(runtime_info.read_text(encoding="utf-8"))
	if sha256(runtime_zip) != info["sha256"]:
		raise ValueError("portable runtime SHA-256 mismatch")
	archive = out.with_suffix(".zip")
	if out.exists() or archive.exists():
		raise ValueError("output already exists; choose a new output path")
	for path in site.rglob("*"):
		if path.is_symlink():
			raise ValueError("site must not contain symlinks: " + str(path))
	out.mkdir(parents=True)
	shutil.copytree(site, out / "site")
	root = Path(__file__).resolve().parent.parent
	for name in ("server.py", "Start CPS.cmd", "README.md"):
		shutil.copyfile(root / "tools" / "offline_cps" / name, out / name)
	# cmd.exe expects CRLF even when the source checkout uses LF.
	launcher = out / "Start CPS.cmd"
	launcher.write_bytes(launcher.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
	shutil.copyfile(root / "LICENSE", out / "LICENSE-RadioFoundry.txt")
	runtime = out / "runtime"
	runtime.mkdir()
	with zipfile.ZipFile(runtime_zip) as z:
		for entry in z.infolist():
			p = Path(entry.filename)
			if p.is_absolute() or ".." in p.parts or ":" in entry.filename or not (runtime / p).resolve().is_relative_to(runtime.resolve()):
				raise ValueError("unsafe runtime archive path")
			z.extract(entry, runtime)
	if not (runtime / "python.exe").is_file() or not (runtime / "LICENSE.txt").is_file():
		raise ValueError("runtime ZIP is missing Python or its license")
	files = sorted(p for p in out.rglob("*") if p.is_file())
	digests = {p.relative_to(out).as_posix(): sha256(p) for p in files}
	package_id = hashlib.sha256(json.dumps(digests, sort_keys=True).encode()).hexdigest()[:16]
	git = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True, check=True).stdout.strip()
	build = {"app": "RadioFoundry Offline CPS", "package_id": package_id,
		"built_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "git": git,
		"runtime": info, "files": len(files), "launcher": "Start CPS.cmd", "url": "http://localhost:8765/codeplug/"}
	(out / "build-info.json").write_text(json.dumps(build, indent=2) + "\n", encoding="utf-8")
	digests["build-info.json"] = sha256(out / "build-info.json")
	(out / "SHA256SUMS").write_text("".join(f"{h}  {name}\n" for name, h in sorted(digests.items())), encoding="utf-8")
	with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
		for p in sorted(out.rglob("*")):
			if p.is_file():
				z.write(p, (Path(out.name) / p.relative_to(out)).as_posix())
	print(json.dumps({"archive": str(archive), "bytes": archive.stat().st_size, "sha256": sha256(archive), "package_id": package_id}, indent=2))
	return archive


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--site", type=Path, required=True)
	ap.add_argument("--runtime-zip", type=Path, required=True)
	ap.add_argument("--runtime-info", type=Path, required=True)
	ap.add_argument("--out", type=Path, required=True)
	a = ap.parse_args()
	try:
		package(a.site.resolve(), a.runtime_zip.resolve(), a.runtime_info.resolve(), a.out.resolve())
	except (ValueError, OSError, zipfile.BadZipFile) as e:
		ap.exit(1, str(e) + "\n")


if __name__ == "__main__":
	main()

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Bounded firmware build queue. Runs inside the compiler container, never with a Docker socket."""
import argparse
from collections import deque
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import queue
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release_version

NAME = re.compile(r"[A-Z][A-Z0-9_]{0,63}")
JOB = re.compile(r"[a-f0-9]{64}")


class RequestError(ValueError):
    pass


def normalize(request, product, radio, features):
    if not isinstance(request, dict) or request.get("board") != product["board"]:
        raise RequestError("Unsupported radio")
    version = request.get("ref")
    if not isinstance(version, str) or len(version) > 80 or release_version.parse(version) is None:
        raise RequestError("Choose a published release version")
    wanted = request.get("features")
    known = {f["name"] for f in features}
    if not isinstance(wanted, list) or len(wanted) > len(known) or any(
            not isinstance(n, str) or not NAME.fullmatch(n) or n not in known for n in wanted):
        raise RequestError("Unknown feature")
    if len(set(wanted)) != len(wanted):
        raise RequestError("Duplicate feature")
    wanted = set(wanted)
    unavailable = set(radio["unsupported"]) | set(radio.get("not_written", [])) | {"AUDIO_PROBE"}
    if wanted & unavailable:
        raise RequestError("Selection includes an unavailable feature")
    required = set(product["build_required"]) | set(radio.get("required", []))
    if not required <= wanted:
        raise RequestError("Programming and recovery features must remain enabled")
    if request.get("options") not in (None, {}):
        raise RequestError("Build options are not supported by this product builder")
    for feature in features:
        if feature["name"] not in wanted:
            continue
        needs = feature.get("needs", {})
        if not set(needs.get("all", [])) <= wanted or (needs.get("any") and not wanted.intersection(needs["any"])):
            raise RequestError("Unmet dependencies for " + feature["name"])
    if not isinstance(request.get("testing", False), bool):
        raise RequestError("Invalid release channel")
    return {"board": product["board"], "ref": version, "testing": request.get("testing", False),
            "features": sorted(wanted), "options": {}}


def cache_key(selection, compiler):
    return hashlib.sha256(json.dumps({"schema": 1, "compiler": compiler, **selection}, sort_keys=True).encode()).hexdigest()


class Builder:
    def __init__(self, state, product, radio, features, compiler, runner=None, catalog_loader=None):
        self.state = Path(state).resolve()
        self.state.mkdir(parents=True, exist_ok=True)
        self.product, self.radio, self.features = product, radio, features
        self.compiler = compiler
        self.runner = runner or self.compile
        self.catalog_loader, self.catalog_at = catalog_loader, time.monotonic()
        self.lock = threading.RLock()
        self.jobs = {}
        self.pending = queue.Queue(maxsize=4)
        self.requests = deque()
        threading.Thread(target=self.work, daemon=True).start()

    def submit(self, request):
        with self.lock:
            if self.catalog_loader and time.monotonic() - self.catalog_at >= 60:
                self.radio, self.features = self.catalog_loader()
                self.catalog_at = time.monotonic()
        selection = normalize(request, self.product, self.radio, self.features)
        identity = cache_key(selection, self.compiler + ":" + self.product.get("repository", ""))
        with self.lock:
            job = self.jobs.get(identity)
            if job and job["status"] != "failed":
                return dict(job)
            directory = self.state / identity
            if (directory / "result.json").is_file():
                try:
                    self.check_result(directory, selection)
                    result = self.done(identity)
                    self.jobs[identity] = result
                    return dict(result)
                except (OSError, ValueError, KeyError):
                    pass
            now = time.monotonic()
            while self.requests and self.requests[0] < now - 60:
                self.requests.popleft()
            if len(self.requests) >= 6 or self.pending.full():
                raise RequestError("Build queue is full; try again shortly")
            self.requests.append(now)
            ahead = self.pending.qsize() + int(any(j["status"] == "running" for j in self.jobs.values()))
            job = {"id": identity, "status": "queued", "position": ahead}
            self.jobs[identity] = job
            self.pending.put_nowait((identity, selection))
            return dict(job)

    def status(self, identity):
        with self.lock:
            return dict(self.jobs[identity])

    def check_result(self, directory, selection):
        manifest = json.loads((directory / "firmware.json").read_text())
        image = (directory / "firmware.bin").read_bytes()
        if (manifest.get("product") != self.product["name"] or manifest.get("board") != selection["board"]
                or manifest.get("version") != selection["ref"] or manifest.get("variant") != "custom"
                or manifest.get("source_tag") != source_tag(self.product, selection)
                or manifest.get("codec", {}).get("included") is not False
                or manifest.get("selected_features") != selection["features"]
                or sorted(filter(None, manifest.get("features", "").split(","))) != selection["features"]
                or manifest.get("image", {}).get("size") != len(image)
                or manifest["image"]["sha256"] != hashlib.sha256(image).hexdigest()):
            raise ValueError("Build result does not match the request")

    @staticmethod
    def done(identity):
        return {"id": identity, "status": "done", "manifest": f"builds/{identity}/firmware.json",
                "image": f"builds/{identity}/firmware.bin"}

    def work(self):
        while True:
            identity, selection = self.pending.get()
            directory = self.state / identity
            try:
                with self.lock:
                    self.jobs[identity] = {"id": identity, "status": "running"}
                directory.mkdir(exist_ok=True)
                self.runner(selection, directory)
                self.check_result(directory, selection)
                result = self.done(identity)
                (directory / "result.json").write_text(json.dumps(result))
                with self.lock:
                    self.jobs[identity] = result
                self.prune(identity)
            except Exception as error:
                with self.lock:
                    self.jobs[identity] = {"id": identity, "status": "failed", "error": str(error)[-2000:]}
            finally:
                self.pending.task_done()

    def prune(self, current):
        directories = sorted((p for p in self.state.iterdir() if p.is_dir() and JOB.fullmatch(p.name)),
                             key=lambda p: p.stat().st_mtime, reverse=True)
        for path in directories[16:]:
            with self.lock:
                if path.name == current or self.jobs.get(path.name, {}).get("status") in ("queued", "running"):
                    continue
                self.jobs.pop(path.name, None)
                shutil.rmtree(path)

    def compile(self, selection, output):
        # Only release tags from the configured repository; no browser-supplied URL, command, or make variable.
        version = selection["ref"]
        tag = source_tag(self.product, selection)
        deadline = time.monotonic() + 900
        with tempfile.TemporaryDirectory(prefix="firmware-", dir=self.state) as temporary:
            root = Path(temporary)
            log = output / "build.log"

            def run(command, capture=False):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise ValueError("Build exceeded its time limit")
                process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           text=True, start_new_session=os.name != "nt")
                try:
                    text = process.communicate(timeout=remaining)[0]
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        process.kill()
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.communicate()
                    raise ValueError("Build exceeded its time limit") from None
                with log.open("a") as stream:
                    stream.write(text[-128000:])
                if process.returncode:
                    raise ValueError(text[-1800:] or "Build command failed")
                if not capture and re.search(r"\bwarning:", text, re.I):
                    raise ValueError("Compiler warning: " + text[-1800:])
                return text

            log.write_text("")
            run(["git", "init", "-q"])
            run(["git", "remote", "add", "origin", "https://github.com/" + self.product["repository"] + ".git"])
            run(["git", "fetch", "--depth=1", "--no-tags", "origin", "refs/tags/" + tag])
            run(["git", "checkout", "--detach", "-q", "FETCH_HEAD"])
            commit = run(["git", "rev-parse", "HEAD"], capture=True).strip()
            run(["git", "-c", "protocol.file.allow=never", "submodule", "update", "--init", "--depth=1", "--",
                 "third_party/FreeRTOS-Kernel"])
            base = ["make", "-s", "BOARD=" + selection["board"]]
            default = dict(line.split("=", 1) for line in run(base + ["print-config"], capture=True).splitlines() if "=" in line)
            unknown = set(selection["features"]) - default.keys()
            if unknown:
                raise ValueError("unknown feature in this release: " + ", ".join(sorted(unknown)))
            unavailable = set(run(base + ["print-unsupported"], capture=True).split()) | set(
                run(base + ["print-not-written"], capture=True).split()) | {"AUDIO_PROBE"}
            if unavailable.intersection(selection["features"]):
                raise ValueError("Feature is unavailable in this release")
            flags = [f"CONFIG_{name}={'y' if name in selection['features'] else 'n'}" for name in sorted(default)]
            command = base + flags + ["RELEASE=" + version]
            resolved = run(command + ["print-config"], capture=True)
            enabled = sorted(line.split("=")[0] for line in resolved.splitlines() if line.endswith("=y"))
            if enabled != selection["features"]:
                raise ValueError("Resolved firmware options do not match the request")
            run(command + ["-j1"])
            run(command + ["stack-check"])
            image = root / "build" / selection["board"] / ("rfoundry_" + selection["board"] + ".bin")
            run(["python3", "tools/release.py", "--board", selection["board"], "--product", self.product["name"],
                 "--version", version, "--git", commit, "--features", ",".join(enabled), "--bin", str(image), "--out", "dist"])
            manifest_path = next((root / "dist").glob("*.json"))
            manifest = json.loads(manifest_path.read_text())
            manifest.update(variant="custom", selected_features=enabled, source_tag=tag,
                            off=",".join(sorted(default.keys() - set(enabled) - unavailable)))
            manifest["image"]["file"] = "firmware.bin"
            shutil.copyfile(image, output / "firmware.bin")
            (output / "firmware.json").write_text(json.dumps(manifest))
            # Successful jobs retain only artifacts and a small status file.
            log.unlink(missing_ok=True)


def handler(builder):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            if self.path != "/v1/builds":
                return self.reply(404, {"detail": "Not found"})
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 32768:
                    raise RequestError("Invalid request size")
                self.connection.settimeout(10)
                request = json.loads(self.rfile.read(size))
                return self.reply(200, builder.submit(request))
            except (ValueError, TypeError) as error:
                return self.reply(400, {"detail": str(error)})

        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            if path == "/v1/health":
                return self.reply(200, {"status": "ok"})
            match = re.fullmatch(r"/v1/builds/([a-f0-9]{64})(?:/(firmware\.(?:bin|json)))?", path)
            try:
                if not match:
                    raise KeyError()
                identity, file = match.groups()
                status = builder.status(identity)
                if file is None:
                    return self.reply(200, status)
                if status["status"] != "done":
                    raise KeyError()
                body = (builder.state / identity / file).read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "application/json" if file.endswith("json") else "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
            except ConnectionError:
                pass
            except (KeyError, OSError):
                self.reply(404, {"detail": "Build not found"})

        def log_message(self, *_):
            pass
    return Handler


def source_tag(product, selection):
    if release_version.parse(selection["ref"])[0] == 0:
        return ("RC-" if selection.get("testing") else "") + product["prefix"] + "-" + selection["ref"]
    return selection["ref"]


def catalog_loader(base, product):
    def load():
        def get(path):
            request = urllib.request.Request(base.rstrip("/") + "/data/" + path,
                                             headers={"User-Agent": product["name"] + "-firmware-builder/1.0"})
            with urllib.request.urlopen(request, timeout=5) as response:
                body = response.read(2 * 1024 * 1024 + 1)
            if len(body) > 2 * 1024 * 1024:
                raise RequestError("Feature catalog is too large")
            return json.loads(body)
        try:
            radio = next(r for r in get("radios.json")["radios"] if r["id"] == product["board"])
            return radio, get("features.json")["features"]
        except (OSError, ValueError, KeyError, StopIteration) as error:
            raise RequestError("Firmware feature catalog is unavailable") from error
    return load


def clean_workspaces(state):
    state = Path(state).resolve()
    for path in state.glob("firmware-*"):
        if not path.is_symlink() and path.is_dir() and path.resolve().parent == state:
            shutil.rmtree(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    catalog = parser.add_mutually_exclusive_group(required=True)
    catalog.add_argument("--site", type=Path)
    catalog.add_argument("--site-url")
    parser.add_argument("--product", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    product = json.loads(args.product.read_text())
    loader = None
    if args.site_url:
        loader = catalog_loader(args.site_url, product)
        radio, features = loader()
    else:
        radios = json.loads((args.site / "data/radios.json").read_text())["radios"]
        radio = next(r for r in radios if r["id"] == product["board"])
        features = json.loads((args.site / "data/features.json").read_text())["features"]
    # A restarted container must not leave gigabytes of interrupted compiler work behind.
    # The process lock also prevents two workers from sharing a cache/workspace directory.
    import fcntl
    args.state.mkdir(parents=True, exist_ok=True)
    with (args.state / ".worker.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        clean_workspaces(args.state)
        builder = Builder(args.state, product, radio, features, args.compiler, catalog_loader=loader)
        ThreadingHTTPServer(("0.0.0.0", args.port), handler(builder)).serve_forever()


if __name__ == "__main__":
    main()

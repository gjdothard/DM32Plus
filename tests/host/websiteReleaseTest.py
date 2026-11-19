#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Website release identity, complete bundles, and failed-deployment rollback."""
import hashlib
from io import BytesIO
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("website_package", ROOT / "tools/website/package.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
sys.path.insert(0, str(ROOT / "tools"))
update_spec = importlib.util.spec_from_file_location("website_update", ROOT / "tools/website/update.py")
updater = importlib.util.module_from_spec(update_spec)
update_spec.loader.exec_module(updater)
sys.path.insert(0, str(ROOT / "tools/website"))
privileged_spec = importlib.util.spec_from_file_location("website_privileged", ROOT / "tools/website/privileged.py")
privileged = importlib.util.module_from_spec(privileged_spec)
privileged_spec.loader.exec_module(privileged)


class WebsiteReleaseTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.site = self.root / "site"
        for name in ("index.html", "codeplug/index.html", "LICENSE"):
            path = self.site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("website", encoding="utf-8")
        self.product = self.root / "product.json"
        self.product.write_text(json.dumps(dict(name="Example", prefix="example", board="sample")), encoding="utf-8")
        self.image = self.site / "firmware/sample/example-sample-v1.0.0.bin"
        self.image.parent.mkdir(parents=True)
        self.image.write_bytes(b"test image")
        self.manifest = self.image.with_suffix(".json")
        self.manifest.write_text(json.dumps(dict(product="Example", board="sample", version="v1.0.0", variant="full", git_full="a" * 40,
            codec={"included": False}, image={"file": self.image.name, "sha256": hashlib.sha256(self.image.read_bytes()).hexdigest()})), encoding="utf-8")
        self.archive = self.root / "website.tar.gz"

    def bundle(self):
        module.package(self.site, self.product, "v1.0.0", self.archive)

    def test_complete_bundle_and_hashes(self):
        self.bundle()
        with tarfile.open(self.archive) as archive:
            names = archive.getnames()
            for name in ("server.py", "release_version.py", "release-products.json", "serve.py", "compose.yaml"):
                self.assertIn(name, names)
            for line in archive.extractfile("SHA256SUMS").read().decode().splitlines():
                digest, name = line.split("  ", 1)
                self.assertEqual(hashlib.sha256(archive.extractfile(name).read()).hexdigest(), digest)

    def test_wrong_release_and_corrupt_image_refused(self):
        with self.assertRaises(FileNotFoundError):
            module.package(self.site, self.product, "v1.0.1", self.archive)
        self.image.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.bundle()
        self.assertFalse(self.archive.exists())

    def test_testing_bundle_has_banner_and_source_identity(self):
        (self.site / "index.html").write_text("<html><body>Example</body></html>", encoding="utf-8")
        module.package(self.site, self.product, "v1.0.0", self.archive,
                       channel="Testing", public_url="https://testing.example.com")
        with tarfile.open(self.archive) as archive:
            marker = json.load(archive.extractfile("site/deployment.json"))
            self.assertEqual(marker["channel"], "Testing")
            self.assertEqual(marker["site_git"], "a" * 40)
            self.assertEqual(marker["public_url"], "https://testing.example.com")
            self.assertIn(b'id="testing-cps-banner"', archive.extractfile("site/index.html").read())
        with self.assertRaises(ValueError):
            module.package(self.site, self.product, "v1.0.0", self.archive,
                           channel="Testing", public_url="http://insecure.example.com")

    def test_updater_selects_only_newer_complete_finals(self):
        release = dict(tag_name="v1.0.0", draft=False, prerelease=False, assets=[dict(
            name="website-v1.0.0.tar.gz", state="uploaded", digest="sha256:" + "a" * 64)])
        self.assertEqual(updater.selected_asset(release, "v26.11"), ("v1.0.0", "website-v1.0.0.tar.gz", "a" * 64))
        for current in ("v1.0.0", "v1.0.1", "v2.0.0"):
            self.assertIsNone(updater.selected_asset(release, current))
        for changes in (dict(prerelease=True), dict(draft=True), dict(tag_name="v1.0.0-rc.1"), dict(assets=[]),
                        dict(assets=[{**release["assets"][0], "digest": ""}])):
            self.assertIsNone(updater.selected_asset({**release, **changes}))

    def test_privileged_helper_refuses_unsafe_archives(self):
        self.bundle()
        privileged.validate_archive(self.archive)
        for name, kind, mode in (("../outside", tarfile.REGTYPE, 0o644),
                                 ("site/link", tarfile.SYMTYPE, 0o644),
                                 ("site/writable", tarfile.REGTYPE, 0o666),
                                 ("unauthorized", tarfile.REGTYPE, 0o644)):
            bad = self.root / "bad.tar.gz"
            with tarfile.open(bad, "w:gz") as archive:
                info = tarfile.TarInfo(name)
                info.type, info.mode, info.linkname = kind, mode, "/etc/passwd"
                archive.addfile(info, BytesIO())
            with self.subTest(name=name), self.assertRaises(ValueError):
                privileged.validate_archive(bad)

    def test_privileged_helper_rechecks_github_and_uses_fixed_compose(self):
        self.bundle()
        content = self.archive.read_bytes()
        release = dict(tag_name="v1.0.0", draft=False, prerelease=False, assets=[dict(
            name="website-v1.0.0.tar.gz", state="uploaded", digest="sha256:" + hashlib.sha256(content).hexdigest())])
        config = dict(repository="owner/project", deployment_root=str(self.root / "deployed"))
        header = b'{"version":"v1.0.0"}\n'
        with patch.object(privileged.urllib.request, "urlopen", return_value=BytesIO(json.dumps(release).encode())), \
                patch.object(privileged.subprocess, "run") as run:
            privileged.deploy(BytesIO(header + content), config, self.root)
            self.assertEqual(run.call_args.kwargs["env"]["WEBSITE_COMPOSE_FILE"], "/etc/website-updater/compose.yaml")
            self.assertEqual(run.call_args.args[0][:2], ["/bin/sh", "/usr/local/lib/website-updater/deploy.sh"])
        with patch.object(privileged.urllib.request, "urlopen", return_value=BytesIO(json.dumps(release).encode())), \
                patch.object(privileged.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "digest"):
                privileged.deploy(BytesIO(header + b"tampered"), config, self.root)
            run.assert_not_called()

    @unittest.skipUnless(os.name == "posix", "host socket transport runs on Linux")
    def test_socket_transport_under_no_new_privileges(self):
        self.bundle()
        address = self.root / "deploy.sock"
        received, failures = [], []

        def check_payload(stream, config, work):
            received.append((json.loads(stream.readline()), stream.read()))

        def serve(listener):
            try:
                connection, _ = listener.accept()
                with connection:
                    privileged.serve_connection(connection, {}, self.root)
            except Exception as error:
                failures.append(error)

        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(str(address))
            listener.listen(1)
            listener.settimeout(10)
            with patch.object(privileged, "deploy", side_effect=check_payload):
                thread = threading.Thread(target=serve, args=(listener,))
                thread.start()
                code = '''import ctypes, sys
from pathlib import Path
sys.path[:0] = [sys.argv[1], str(Path(sys.argv[1]).parent)]
from update import send_bundle
if ctypes.CDLL(None).prctl(38, 1, 0, 0, 0) != 0:
    raise SystemExit("cannot set no-new-privileges")
send_bundle(Path(sys.argv[2]), "v1.0.0", sys.argv[3])
'''
                result = subprocess.run([sys.executable, "-c", code, str(ROOT / "tools/website"), str(self.archive), str(address)], capture_output=True, timeout=15)
                thread.join(timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertFalse(thread.is_alive())
        self.assertEqual(failures, [])
        self.assertEqual(received, [({"version": "v1.0.0"}, self.archive.read_bytes())])

    @unittest.skipUnless(os.name == "posix", "host socket transport runs on Linux")
    def test_socket_helper_reports_deployment_failure(self):
        client, server = socket.socketpair()
        with client, server, patch.object(privileged, "deploy", side_effect=ValueError("rejected bundle")):
            with self.assertRaisesRegex(ValueError, "rejected bundle"):
                privileged.serve_connection(server, {}, self.root)
            self.assertEqual(json.loads(client.recv(4096)), {"ok": False, "error": "rejected bundle"})

    @unittest.skipUnless(os.name == "posix" and shutil.which("flock"), "deployment runs on Linux")
    def test_deploy_and_failed_health_check_restore_every_file(self):
        self.bundle()
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        docker = bin_dir / "docker"
        docker.write_text('#!/bin/sh\ncase " $* " in *" up "*) if [ -f .fail-once ]; then rm .fail-once; exit 1; fi ;; esac\n', encoding="utf-8")
        docker.chmod(0o755)
        env = {**os.environ, "PATH": str(bin_dir) + ":" + os.environ["PATH"]}
        deployed = self.root / "deployed"
        deployed.mkdir()
        if getattr(self, "controlled", False):
            env["WEBSITE_COMPOSE_FILE"] = "/etc/website-updater/compose.yaml"
            (deployed / "compose.yaml").write_text("operator config", encoding="utf-8")
        args = ["sh", str(ROOT / "tools/website/deploy.sh"), str(deployed), str(self.archive), hashlib.sha256(self.archive.read_bytes()).hexdigest(), "v1.0.0"]
        subprocess.run(args, env=env, check=True, capture_output=True)
        if getattr(self, "controlled", False):
            self.assertEqual((deployed / "compose.yaml").read_text(), "operator config")
        originals = {name: (deployed / name).read_bytes() for name in ("server.py", "serve.py", "release_version.py", "release-products.json", "compose.yaml", "site/index.html")}
        (deployed / "compose.override.yaml").write_text("preserved", encoding="utf-8")
        (deployed / "site/index.html").write_text("previous website", encoding="utf-8")
        originals["site/index.html"] = b"previous website"
        (deployed / ".fail-once").touch()
        result = subprocess.run(args, env=env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        for name, content in originals.items():
            self.assertEqual((deployed / name).read_bytes(), content, name)
        self.assertEqual((deployed / "compose.override.yaml").read_text(), "preserved")
        args[-2] = "0" * 64
        self.assertNotEqual(subprocess.run(args, env=env, capture_output=True).returncode, 0)
        self.assertEqual((deployed / "site/index.html").read_bytes(), b"previous website")

    @unittest.skipUnless(os.name == "posix" and shutil.which("flock"), "deployment runs on Linux")
    def test_privileged_deployment_preserves_the_operator_compose_file(self):
        self.controlled = True
        self.test_deploy_and_failed_health_check_restore_every_file()


if __name__ == "__main__":
    unittest.main()

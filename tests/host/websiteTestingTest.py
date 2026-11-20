#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Testing deployment provenance, archive validation and failed-health rollback."""
import hashlib
from io import BytesIO
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/website"))
import testing_update as updater


class TestingWebsiteTest(unittest.TestCase):
    def test_only_successful_testing_pushes(self):
        run = dict(head_branch="Testing", event="push", status="completed", conclusion="success", head_sha="a" * 40)
        self.assertEqual(updater.completed_run([run]), run)
        for changed in ({"conclusion": "failure"}, {"status": "in_progress"}, {"head_branch": "main"},
                        {"event": "pull_request"}, {"head_sha": "../invalid"}):
            self.assertIsNone(updater.completed_run([{**run, **changed}]))

    def test_only_complete_testing_assets(self):
        asset = dict(name="website-testing-v1.1.0-rc.6.tar.gz", state="uploaded", digest="sha256:" + "a" * 64)
        release = dict(tag_name="v1.1.0-rc.6", draft=False, prerelease=True, assets=[asset])
        self.assertEqual(updater.selected_asset([release])[0][1], "v1.1.0-rc.6")
        for changed in ({"draft": True}, {"prerelease": False}, {"tag_name": "v1.1.0-beta.6"},
                        {"assets": [{**asset, "digest": ""}]}, {"assets": [{**asset, "name": "website-v1.1.0.tar.gz"}]}):
            self.assertEqual(updater.selected_asset([{**release, **changed}]), [])

    def archive(self, path, *, marker=None, extra=None, corrupt=False):
        files = {name: b"sample" for name in ("site/index.html", "site/codeplug/index.html", "site/LICENSE",
                 "serve.py", "server.py", "release_version.py", "release-products.json")}
        files["site/deployment.json"] = json.dumps(marker or dict(channel="Testing", git="a" * 40,
                  site_git="a" * 40, version="v1.1.0-rc.6")).encode()
        sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in files.items())
        files["SHA256SUMS"] = sums.encode()
        if corrupt:
            files["serve.py"] = b"corrupt"
        with tarfile.open(path, "w:gz") as archive:
            for name, data in files.items():
                info = tarfile.TarInfo(name)
                info.size, info.mode = len(data), 0o644
                archive.addfile(info, BytesIO(data))
            if extra:
                archive.addfile(extra)

    def test_bundle_checksums_and_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name, options, valid in [("valid", {}, True), ("corrupt", {"corrupt": True}, False),
                    ("wrong-source", {"marker": dict(channel="Testing", git="b" * 40, site_git="b" * 40, version="v1.1.0-rc.6")}, False)]:
                archive, destination = root / (name + ".tar.gz"), root / name
                destination.mkdir()
                self.archive(archive, **options)
                if valid:
                    updater.unpack(archive, destination, "a" * 40, "v1.1.0-rc.6")
                else:
                    with self.assertRaises(ValueError):
                        updater.unpack(archive, destination, "a" * 40, "v1.1.0-rc.6")

    def test_refuses_traversal_and_links(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name, kind in [("../outside", tarfile.REGTYPE), ("site/link", tarfile.SYMTYPE)]:
                info = tarfile.TarInfo(name)
                info.type, info.mode, info.linkname = kind, 0o644, "/etc/passwd"
                archive = root / "bad.tar.gz"
                self.archive(archive, extra=info)
                with self.assertRaises(ValueError):
                    updater.unpack(archive, root / "out", "a" * 40, "v1.1.0-rc.6")

    def test_failed_health_restores_previous_release(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            previous, candidate = root / "previous", root / "candidate"
            previous.mkdir()
            candidate.mkdir()
            try:
                (root / "current").symlink_to(previous, target_is_directory=True)
            except OSError:
                self.skipTest("symlink creation unavailable on this host")
            with patch.object(updater, "compose") as compose, patch.object(updater, "request_json", return_value={"git": "wrong"}):
                with self.assertRaisesRegex(ValueError, "served website"):
                    updater.activate(root, candidate, {"health_url": "http://localhost/deployment.json"}, "a" * 40)
                self.assertEqual((root / "current").resolve(), previous)
                self.assertEqual(compose.call_count, 2)


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Build requests, duplicate jobs, persistent cache validation, and failures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("firmware_builder", ROOT / "tools/firmware_builder/server.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
PRODUCT = {"board": "sample", "name": "Sample", "prefix": "sample", "build_required": ["LINK"]}
RADIO = {"unsupported": ["USB"], "not_written": ["VOX"], "required": []}
FEATURES = [{"name": n, "needs": {"all": ["A"] if n == "B" else [], "any": []}}
            for n in ("LINK", "A", "B", "USB", "VOX")]
REQUEST = {"board": "sample", "ref": "v1.0.0", "features": ["LINK", "A"], "options": {}}


def artifact(selection, output):
    image = b"sample firmware"
    (output / "firmware.bin").write_bytes(image)
    (output / "firmware.json").write_text(json.dumps({
        "product": "Sample", "board": "sample", "version": selection["ref"], "variant": "custom",
        "source_tag": builder.source_tag(PRODUCT, selection),
        "codec": {"included": False}, "selected_features": selection["features"],
        "features": ",".join(selection["features"]),
        "image": {"size": len(image), "sha256": hashlib.sha256(image).hexdigest()}}))


class Requests(unittest.TestCase):
    def test_legacy_testing_and_stable_tags_are_separate(self):
        self.assertEqual(builder.source_tag(PRODUCT, {"ref": "v26.11", "testing": True}), "RC-sample-v26.11")
        self.assertEqual(builder.source_tag(PRODUCT, {"ref": "v26.11", "testing": False}), "sample-v26.11")
        self.assertEqual(builder.source_tag(PRODUCT, {"ref": "v1.0.0-rc.1", "testing": True}), "v1.0.0-rc.1")

    def test_only_tagged_versions_and_known_compatible_features(self):
        self.assertEqual(builder.normalize(REQUEST, PRODUCT, RADIO, FEATURES)["features"], ["A", "LINK"])
        for change in ({"ref": "main"}, {"ref": "--upload-pack=evil"}, {"board": "other"},
                       {"features": ["LINK", "UNKNOWN"]}, {"features": ["LINK", "USB"]},
                       {"features": ["LINK", "VOX"]}, {"features": ["A"]},
                       {"features": ["LINK", "B"]}, {"features": ["LINK", "LINK"]},
                       {"options": {"CFLAGS": "bad"}}):
            with self.subTest(change=change), self.assertRaises(builder.RequestError):
                builder.normalize({**REQUEST, **change}, PRODUCT, RADIO, FEATURES)

    def test_duplicate_jobs_and_restart_reuse_verified_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            count, started, finish = [], threading.Event(), threading.Event()

            def run(selection, directory):
                count.append(selection)
                started.set()
                self.assertTrue(finish.wait(5))
                artifact(selection, directory)

            service = builder.Builder(temporary, PRODUCT, RADIO, FEATURES, "compiler-1", runner=run)
            first = service.submit(REQUEST)
            self.assertTrue(started.wait(5))
            self.assertEqual(service.submit(REQUEST)["id"], first["id"])
            finish.set()
            service.pending.join()
            self.assertEqual(service.status(first["id"])["status"], "done")
            self.assertEqual(len(count), 1)
            restarted = builder.Builder(temporary, PRODUCT, RADIO, FEATURES, "compiler-1", runner=run)
            self.assertEqual(restarted.submit(REQUEST)["status"], "done")
            self.assertEqual(len(count), 1)

    def test_corrupt_artifacts_are_not_returned_from_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            service = builder.Builder(temporary, PRODUCT, RADIO, FEATURES, "compiler-1", runner=artifact)
            first = service.submit(REQUEST)
            service.pending.join()
            (Path(temporary) / first["id"] / "firmware.bin").write_bytes(b"corrupt")
            restarted = builder.Builder(temporary, PRODUCT, RADIO, FEATURES, "compiler-1", runner=artifact)
            self.assertNotEqual(restarted.submit(REQUEST)["status"], "done")
            restarted.pending.join()
            self.assertEqual(restarted.status(first["id"])["status"], "done")

    def test_failed_build_never_exposes_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            def fail(*_):
                raise ValueError("RAM overflowed")
            service = builder.Builder(temporary, PRODUCT, RADIO, FEATURES, "compiler-1", runner=fail)
            job = service.submit(REQUEST)
            service.pending.join()
            status = service.status(job["id"])
            self.assertEqual(status["status"], "failed")
            self.assertIn("RAM overflowed", status["error"])
            self.assertNotIn("image", status)

    def test_cache_identity_includes_compiler_release_and_features(self):
        selection = builder.normalize(REQUEST, PRODUCT, RADIO, FEATURES)
        old = builder.cache_key(selection, "compiler-1")
        self.assertNotEqual(old, builder.cache_key(selection, "compiler-2"))
        self.assertNotEqual(old, builder.cache_key({**selection, "ref": "v1.0.1"}, "compiler-1"))
        self.assertNotEqual(old, builder.cache_key({**selection, "features": ["LINK"]}, "compiler-1"))


if __name__ == "__main__":
    unittest.main()

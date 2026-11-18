# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Verify the administrator script preserves CPS restrictions and isolates the build worker."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (ROOT / "tools/firmware_builder/install.sh").read_text(encoding="utf-8")
CONFIGURE = SCRIPT.split("<<'PY'\n", 1)[1].split("\nPY\n", 1)[0]


class Installer(unittest.TestCase):
    def configure(self, config, image="registry.example/compiler@sha256:" + "a" * 64,
                  url="https://public.example/"):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "compose.json"
            path.write_text(json.dumps(config))
            code = CONFIGURE.replace("Path('/etc/website-updater/compose.yaml')", "Path(" + repr(str(path)) + ")")
            result = subprocess.run([sys.executable, "-c", code, temporary, image, url], capture_output=True, text=True)
            return result, json.loads(path.read_text())

    def test_preserves_existing_service_and_adds_only_an_internal_limited_worker(self):
        cps = {"image": "python:3.12-slim", "ports": ["127.0.0.1:8082:8080"], "read_only": True,
               "user": "65534:65534", "cap_drop": ["ALL"], "security_opt": ["no-new-privileges:true"],
               "environment": {"CPS_PUBLIC_HOST": "proxy.example"}, "volumes": ["/site:/app/site:ro"]}
        result, config = self.configure({"name": "example", "services": {"cps": cps}})
        self.assertEqual(result.returncode, 0, result.stderr)
        actual = config["services"]["cps"]
        for key in cps:
            if key != "environment":
                self.assertEqual(actual[key], cps[key])
        self.assertEqual(actual["environment"]["CPS_PUBLIC_HOST"], "proxy.example")
        self.assertEqual(actual["environment"]["CPS_BUILD_ORIGINS"], "https://public.example")
        worker = config["services"]["firmware-builder"]
        self.assertEqual(worker["user"], "65534:65534")
        self.assertTrue(worker["read_only"])
        self.assertEqual(worker["cap_drop"], ["ALL"])
        self.assertEqual(worker["security_opt"], ["no-new-privileges:true"])
        self.assertEqual(worker["cpus"], 1)
        self.assertEqual(worker["pids_limit"], 96)
        self.assertNotIn("ports", worker)
        self.assertNotIn("privileged", worker)
        self.assertEqual([v["target"] for v in worker["volumes"] if not v.get("read_only")], ["/state"])
        self.assertNotIn("docker.sock", json.dumps(worker))

    def test_unpinned_images_and_non_https_catalogs_do_not_change_configuration(self):
        original = {"services": {"cps": {"environment": {}}}}
        for changes in ({"image": "compiler:latest"}, {"url": "http://public.example/"},
                        {"url": "https://user:password@public.example/"}, {"url": "https://public.example/path"}):
            with self.subTest(changes=changes):
                result, config = self.configure(original, **changes)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(config, original)


if __name__ == "__main__":
    unittest.main()

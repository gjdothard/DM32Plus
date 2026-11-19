# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Exercise the actual P25 Make selection, C configuration and About generator."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--make", default=os.environ.get("MAKE", "make"))
parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
OPTIONS, UNITTEST_ARGS = parser.parse_known_args()


class P25BuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="p25-build-")
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        self.harness = self.work / "probe.mk"
        self.harness.write_text(
            "# SPDX-License-Identifier: MIT\n"
            "# Copyright (c) 2026 Garrett Dothard\n"
            "include config/features.mk\ninclude config/p25.mk\n"
            "$(info DIRS=$(CORE_DIRS))\n$(info NOTICES=$(ABOUT_MORE))\n"
            "$(info DEFS=$(FEATURE_DEFS))\n"
            ".PHONY: probe\nprobe:\n", encoding="utf-8")

    def probe(self, *settings, fails=False):
        result = subprocess.run(
            [OPTIONS.make, "--no-print-directory", "-s", "-f", str(self.harness), "probe", *settings],
            cwd=ROOT, text=True, capture_output=True)
        if fails:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            return result.stderr
        self.assertEqual(result.returncode, 0, result.stderr)
        return dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)

    def test_source_and_c_configuration_matrix(self):
        cases = [
            ((), 0, 0),
            (("CONFIG_P25=y", "CONFIG_P25_TRUNK=n"), 1, 0),
            (("CONFIG_P25=y",), 1, 1),
            (("CONFIG_P25=n", "CONFIG_P25_TRUNK=n"), 0, 0),
        ]
        for settings, p25, trunk in cases:
            with self.subTest(settings=settings):
                data = self.probe(*settings)
                self.assertEqual(set(data["DIRS"].split()),
                                 {"core/air", "core/fec", "core/p25", "core/mbe"} if p25 else set())
                self.assertEqual(data["NOTICES"].split(), ["core/mbe/THIRD_PARTY"] if p25 else [])
                # Build exactly the selected sources using exactly the emitted feature defines.
                source = self.work / "probe.c"
                checks = {"P25": p25, "P25_TRUNK": trunk, "AIR": p25, "IMBE": p25, "MBE": p25}
                source.write_text('#include "config.h"\n' + "".join(
                    f'_Static_assert(CONFIG_{name} == {value}, "{name}");\n'
                    for name, value in checks.items()) + "int main(void) { return 0; }\n", encoding="utf-8")
                sources = [str(path) for directory in data["DIRS"].split()
                           for path in sorted((ROOT / directory).glob("*.c"))]
                # The restored upstream loaders call the firmware's always-present codeplug/storage layer.
                sources += [str(ROOT / path) for path in (
                    "core/codeplug/codeplug.c", "core/codeplug/dmr_iddb.c", "core/storage/storage.c",
                    "core/codeplug/codeplug_talk.c",
                    "core/codeplug/codeplug_kv.c", "core/codeplug/codeplug_tag.c",
                    "core/codeplug/codeplug_tagged.c", "core/codeplug/codeplug_records.c",
                    "core/radio/radio_p25.c",
                    "tests/host/sim/p25_link_sim.c")]
                output = self.work / ("probe.exe" if os.name == "nt" else "probe")
                result = subprocess.run(
                    shlex.split(OPTIONS.cc) + ["-std=gnu99", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                              "-Wno-unused-parameter", "-Icore", "-Ihal"]
                    + shlex.split(data["DEFS"]) + [str(source)] + sources + ["-o", str(output), "-lm"],
                    cwd=ROOT, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                subprocess.run([str(output)], cwd=ROOT, check=True)

    def test_ui_compiles_without_dmr(self):
        data = self.probe("CONFIG_DMR=n")
        result = subprocess.run(
            shlex.split(OPTIONS.cc) + ["-std=gnu11", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                      "-Wno-unused-parameter", "-Icore", "-Ihal", "-Idrivers"]
            + shlex.split(data["DEFS"])
            + ["-c", "core/ui/ui_screens.c", "-o", str(self.work / "screens.o")],
            cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_vocoder_compiles_without_dmr(self):
        data = self.probe("CONFIG_DMR=n")
        result = subprocess.run(
            shlex.split(OPTIONS.cc) + ["-std=gnu99", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                      "-Icore", "-Ihal", "-Idrivers", "-Iplatform/hrc7000"]
            + shlex.split(data["DEFS"])
            + ["-c", "platform/hrc7000/vocoder.c", "-o", str(self.work / "vocoder.o")],
            cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_p25_codeplug_feature_off_builds(self):
        for settings in [(), ("CONFIG_DMR=n",), ("CONFIG_P25=y", "CONFIG_P25_TRUNK=n")]:
            with self.subTest(settings=settings):
                data = self.probe(*settings)
                for module in ("core/p25/p25_codeplug.c", "core/p25/p25_admission.c", "core/radio/radio_p25.c", "core/codeplug/codeplug_kv.c",
                               "core/codeplug/codeplug_tag.c", "core/codeplug/codeplug_tagged.c",
                               "core/codeplug/codeplug_records.c"):
                    result = subprocess.run(
                        shlex.split(OPTIONS.cc) + ["-std=gnu99", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                                  "-Icore", "-Ihal"]
                        + shlex.split(data["DEFS"])
                        + ["-c", module, "-o", str(self.work / (Path(module).stem + ".o"))],
                        cwd=ROOT, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_resource_probe_markers(self):
        for settings, enabled, trunk in [
            ((), False, False),
            (("CONFIG_P25=y", "CONFIG_P25_TRUNK=n"), True, False),
            (("CONFIG_P25=y",), True, True),
        ]:
            with self.subTest(settings=settings):
                data = self.probe(*settings)
                output = self.work / "resources.o"
                result = subprocess.run(
                    shlex.split(OPTIONS.cc) + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
                                              "-Icore", "-Ihal"]
                    + shlex.split(data["DEFS"])
                    + ["-c", "tools/p25_resource_probe.c", "-o", str(output)],
                    cwd=ROOT, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                symbols = output.read_bytes()
                for name in ("voice", "engine", "session", "softmodem", "workspace", "workspace_lease", "owner"):
                    self.assertEqual(("p25_bytes_" + name).encode() in symbols, enabled)
                self.assertEqual(b"p25_bytes_follower" in symbols, trunk)

    def test_invalid_protocol_dependencies(self):
        self.assertIn("CONFIG_P25_TRUNK=y needs CONFIG_P25=y",
                      self.probe("CONFIG_P25=n", "CONFIG_P25_TRUNK=y", fails=True))
        self.assertIn("CONFIG_P25=y needs CONFIG_DMR=y",
                      self.probe("CONFIG_DMR=n", "CONFIG_P25=y", fails=True))

    def test_air_provider_compiles_disabled(self):
        data = self.probe("CONFIG_P25=n", "CONFIG_P25_TRUNK=n")
        result = subprocess.run(
            shlex.split(OPTIONS.cc) + ["-std=gnu99", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                      "-Icore", "-Ihal", "-Idrivers", "-c"]
            + shlex.split(data["DEFS"]) + [str(ROOT / "drivers/modem/hrc7000/hrc7000_air.c"),
                                           "-o", str(self.work / "provider.o")],
            cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_derived_switches_cannot_be_overridden(self):
        for name in ("AIR", "IMBE", "MBE"):
            with self.subTest(name=name):
                self.assertIn(f"CONFIG_{name} is derived", self.probe(f"CONFIG_{name}=y", fails=True))

    def test_modem_glue_compiles_without_dmr(self):
        data = self.probe("CONFIG_DMR=n", "CONFIG_P25=n", "CONFIG_P25_TRUNK=n")
        result = subprocess.run(
            shlex.split(OPTIONS.cc) + ["-std=gnu99", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                      "-Icore", "-Ihal", "-Idrivers", "-c"]
            + shlex.split(data["DEFS"]) + [str(ROOT / "platform/hrc7000/dmr_glue.c"),
                                           "-o", str(self.work / "glue.o")],
            cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_optional_about_notice_and_idempotence(self):
        output = self.work / "about.h"
        command = [sys.executable, str(ROOT / "tools/gen_about.py"), str(ROOT / "AUTHORS"),
                   str(ROOT / "platform/hrc7000/THIRD_PARTY"), str(output)]
        subprocess.run(command, check=True, capture_output=True)
        base = output.read_text(encoding="utf-8")
        self.assertNotIn('"mbelib"', base)
        with_codec = command + [str(ROOT / "core/mbe/THIRD_PARTY")]
        subprocess.run(with_codec, check=True, capture_output=True)
        self.assertIn('{ "mbelib", "ISC" }', output.read_text(encoding="utf-8"))
        before = output.stat().st_mtime_ns
        repeated = subprocess.run(with_codec, check=True, capture_output=True, text=True)
        self.assertEqual(repeated.stdout, "")
        self.assertEqual(output.stat().st_mtime_ns, before)
        subprocess.run(command, check=True, capture_output=True)
        self.assertEqual(output.read_text(encoding="utf-8"), base)

    def test_pcm_and_aprs_feature_off_builds(self):
        for settings in [("CONFIG_DMR=n", "CONFIG_P25=n"),
                         ("CONFIG_P25=y", "CONFIG_APRS=n")]:
            with self.subTest(settings=settings):
                data = self.probe(*settings)
                for module in ("pcm", "afsk"):
                    result = subprocess.run(
                        shlex.split(OPTIONS.cc) + ["-std=gnu99", "-Wall", "-Wextra", "-Wshadow", "-Werror",
                                                  "-Icore", "-Ihal", "-Idrivers", "-Iplatform/hrc7000",
                                                  "-Itests/host/stubs/modem_rtos", "-c"]
                        + shlex.split(data["DEFS"])
                        + [str(ROOT / f"tests/host/sim/{module}_platform_sim.c"),
                           "-o", str(self.work / f"{module}.o")],
                        cwd=ROOT, text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    if not shutil.which(OPTIONS.make):
        parser.error(f"Make executable not found: {OPTIONS.make}")
    unittest.main(argv=[sys.argv[0], *UNITTEST_ARGS])

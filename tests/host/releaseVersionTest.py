#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Release migration, channel counters and invalid names without a compiler."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import release_version as rv

class ReleaseVersionTest(unittest.TestCase):
    def test_order_and_migration(self):
        names = ["v99.12b9", "v99.12", "v1.0.0-beta.2", "v1.0.0-beta.10", "v1.0.0-rc.2", "v1.0.0-rc.10", "v1.0.0", "v1.0.1", "v2.0.0"]
        self.assertEqual(sorted(reversed(names), key=rv.parse), names)
        for name in ("v1.0", "v01.0.0", "v1.0.0-beta.0", "v1.0.0-rc.01", "v1.0.0-foo.1", "v1.0.0+build", "v1.0.0/other", None):
            self.assertIsNone(rv.parse(name), name)

    def test_channel_counters(self):
        tags = ["v1.0.0-beta.2", "v1.0.0-beta.10", "v1.0.0-rc.2", "v1.1.0-beta.99", "v1.0.0-beta.01"]
        self.assertEqual(rv.next_prerelease("v1.0.0", "beta", iter(tags)), "v1.0.0-beta.11")
        self.assertEqual(rv.next_prerelease("v1.0.0", "rc", tags), "v1.0.0-rc.3")
        self.assertEqual(rv.next_prerelease("v1.1.0", "rc", tags), "v1.1.0-rc.1")
        for base, channel, tags in (("v1.0.0", "beta", ["v1.0.0"]), ("v26.11", "rc", []), ("v1.0.0-rc.1", "rc", []), ("v1.0.0", "alpha", [])):
            with self.assertRaises(ValueError):
                rv.next_prerelease(base, channel, tags)

    def test_tag_compatibility(self):
        for tag, version in (("v1.0.0", "v1.0.0"), ("v1.0.0-rc.1", "v1.0.0-rc.1"), ("sample-v26.11b1", "v26.11b1"), ("RC-sample-v26.11b1", "v26.11b1")):
            self.assertEqual(rv.from_tag(tag, "sample"), version)
        for tag in ("other-v26.11b1", "sample-v1.0.0", "main", "v26.11", None):
            self.assertIsNone(rv.from_tag(tag, "sample"))

if __name__ == "__main__":
    unittest.main()

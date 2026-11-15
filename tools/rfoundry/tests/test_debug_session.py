# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Session provisioning protocol tests; no serial connection or radio."""
import pathlib
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
import rfoundry_debug as debug


class SessionTests(unittest.TestCase):
    def test_full_key_and_random_seed(self):
        link = types.SimpleNamespace(radio=mock.Mock(), view=mock.Mock(side_effect=[
            {"dmr.aes256": "1", "dmr.state": "idle", "dmr.aes_serial": "0"},
            {"dmr.aes_key_id": "7", "dmr.aes_serial": "1"},
            {"dmr.aes256": "1", "dmr.state": "idle", "dmr.aes_serial": "1"},
            {"dmr.aes_key_id": "255", "dmr.aes_serial": "2"},
        ]))
        key = bytes(range(32))
        with mock.patch.object(debug.secrets, "randbelow", side_effect=[0, 0xFFFFFFFE]) as random:
            debug.DebugLink.dmr_aes256(link, 7, key)
            debug.DebugLink.dmr_aes256(link, 255, key)
        self.assertEqual(link.radio.call_args_list, [
            mock.call(debug.OP_DMR_AES256, struct.pack("<BI", 7, 1) + key),
            mock.call(debug.OP_DMR_AES256, struct.pack("<BI", 255, 0xFFFFFFFF) + key),
        ])
        self.assertEqual(random.call_args_list, [mock.call(0xFFFFFFFF)] * 2)

    def test_invalid_input_does_not_send(self):
        link = types.SimpleNamespace(radio=mock.Mock())
        for key_id, key in [(0, bytes(32)), (256, bytes(32)), (1, bytes(31)), (1, bytes(33))]:
            with self.subTest(key_id=key_id, size=len(key)), self.assertRaises(ValueError):
                debug.DebugLink.dmr_aes256(link, key_id, key)
        link.radio.assert_not_called()

    def test_same_id_needs_provisioning_acknowledgment(self):
        # The old key ID alone cannot acknowledge a queued replacement of that key.
        view = {"dmr.aes256": "1", "dmr.state": "idle", "dmr.aes_serial": "8", "dmr.aes_key_id": "7"}
        link = types.SimpleNamespace(radio=mock.Mock(), view=mock.Mock(return_value=view))
        with mock.patch.object(debug.time, "sleep"), self.assertRaises(debug.DebugLinkError):
            debug.DebugLink.dmr_aes256(link, 7, bytes(32))
        link.radio.assert_called_once()

    def test_busy_radio_does_not_queue_key(self):
        link = types.SimpleNamespace(radio=mock.Mock(), view=mock.Mock(return_value={
            "dmr.aes256": "1", "dmr.state": "rx", "dmr.aes_serial": "8"}))
        with self.assertRaises(debug.DebugLinkError):
            debug.DebugLink.dmr_aes256(link, 7, bytes(32))
        link.radio.assert_not_called()

    def test_cli_reads_key_file(self):
        link = types.SimpleNamespace(dmr_aes256=mock.Mock())
        key = bytes(range(32))
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "synthetic-key.txt"
            path.write_text(key.hex() + "\n", encoding="ascii")
            debug.cmd_radio(link, types.SimpleNamespace(op="aes256", args=["7", str(path)]))
        link.dmr_aes256.assert_called_once_with(7, key)


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Read-only managed-programming capability checks; no session or write starts."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("cps_managed")
def test_managed_programming_capability(radio):
    _, info = radio.link.request(0x1C, bytes([0]))
    assert len(info) >= 26 and info[0] == 1, info
    capacity = int.from_bytes(info[24:26], "little")
    if not capacity:
        skip("no_hw", "this flash map has no staged programming area")
    assert info[1] & 0x05 == 0x05, info
    view = radio.link.view(["core"])
    assert view.get("programming") in ("0", "1"), view

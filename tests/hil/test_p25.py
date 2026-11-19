# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Read-only visibility checks for preprogrammed P25 channels."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("p25")
def test_p25_selected_owner(radio):
    view = radio.link.view(["core"])
    if "p25.active" not in view:
        skip("not_built", "P25 owner visibility is unavailable")
    if view.get("mode") != "p25":
        skip("state", "select a stored P25 channel before this read-only check")
    assert view["p25.active"] == "1", view
    assert view["p25.rx"] in ("0", "1"), view
    assert view.get("vfo") == "0", view


@pytest.mark.feature("p25_trunk")
def test_p25_trunk_selected_owner(radio):
    view = radio.link.view(["core"])
    if "p25.trunk.state" not in view:
        skip("not_built", "P25 trunking owner visibility is unavailable")
    if view.get("mode") != "p25" or view["p25.trunk.state"] == "0":
        skip("state", "select a stored trunked P25 channel before this read-only check")
    assert view["p25.active"] == "1", view
    assert view["p25.trunk.state"] in ("1", "2", "3"), view
    assert 0 <= int(view["p25.trunk.reg"]) <= 8, view
    assert 0 <= int(view["p25.trunk.req"]) <= 6, view


@pytest.mark.feature("rx_extended")
@pytest.mark.feature("p25_trunk")
def test_extended_trunk_passive(radio):
    """Inspect an already selected extended-band system without tuning or PTT."""
    view = radio.link.view(["core"])
    if "p25.trunk.state" not in view:
        skip("not_built", "P25 trunking visibility is unavailable")
    if (view.get("mode") != "p25" or view["p25.trunk.state"] == "0"
            or int(view.get("rx_hz", "0")) < 740000000):
        skip("state", "select an extended-band trunked channel before this read-only check")
    assert view["p25.trunk.reg"] == "0", view
    assert view["p25.trunk.req"] == "0", view

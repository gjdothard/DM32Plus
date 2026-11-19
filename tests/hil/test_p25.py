# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Read-only visibility checks for preprogrammed P25 channels."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("p25_ui")
def test_p25_monitor_pages(radio):
    """Open every monitoring page without changing filters or stored favorites."""
    core = radio.link.view(["core"])
    if core.get("mode") != "p25":
        skip("state", "select a stored P25 channel before this check")
    if radio.link.view(["link"]).get("ui.locked") == "1":
        skip("state", "unlock the keys before checking P25 navigation")

    def press(name):
        # Longer than the minimum synthetic tap: the live decoder can delay a keypad scan.
        radio.link.key_hold(name, 250)
        radio.link.wait_keys_idle()

    def screen(name):
        radio.wait_for(lambda v: v.get("ui.screen") == name, sections=("link",), what=name)

    try:
        radio.go_home()
        press("quick")
        screen("p25-listen")
        if "DMR_TALKGROUP_POOL" in radio.link.version().get("features", "").split(","):
            press("8")
            screen("p25-talkgroups")
            radio.go_home()
            press("quick")
            screen("p25-listen")
        press("6")
        screen("p25-system")
        press("context")
        radio.wait_for(lambda v: v.get("ui.count") == "4", sections=("link",), what="P25 decode details")
        radio.go_home()
        press("context")
        screen("p25-recent")
        radio.go_home()
        press("quick")
        screen("p25-listen")
        press("7")
        screen("p25-recent")
    finally:
        radio.go_home()


@pytest.mark.feature("p25_ui")
def test_p25_monitor_snapshot(radio):
    """Read the published monitoring view without selecting a channel or transmitting."""
    core = radio.link.view(["core"])
    if core.get("mode") != "p25":
        skip("state", "select a stored P25 channel before this read-only check")
    view = radio.link.view(["p25"])
    if view.get("p25.ui") != "1":
        skip("not_built", "P25 monitoring UI is disabled")
    assert view["p25.active"] == "1", view
    assert view["p25.channel"] == core["channel_number"], view
    assert 0 <= int(view["p25.recent"]) <= 4, view
    assert 0 <= int(view["p25.favorites"]) <= 4, view
    assert 0 <= int(view["p25.listen"]) <= 3, view


@pytest.mark.feature("p25")
def test_p25_receive_diagnostic_visibility(radio):
    if "DIAG" not in radio.link.version().get("features", "").split(","):
        skip("not_built", "receive diagnostics are disabled")
    view = radio.link.view(["core"])
    if view.get("mode") != "p25":
        skip("state", "select a stored P25 channel before this read-only check")
    for key in ("p25.tuned_hz", "p25.chunks", "p25.symbols", "p25.syncs", "p25.locks",
                "p25.rejects", "p25.frames", "p25.blocks", "p25.crc_fail", "ch_read_errors", "serial"):
        assert key in view and int(view[key]) >= 0, (key, view)


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

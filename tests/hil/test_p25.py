# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Passive P25 visibility and reversible monitoring controls."""
import time

import pytest

from rfoundry_hil import skip
from rfoundry_debug import DOWN


@pytest.mark.feature("p25_ui")
def test_p25_site_name_visibility(radio):
    """Read the serving-site presentation without changing channels or transmitting."""
    view = radio.link.view(["core", "p25"])
    if "p25.site_state" not in view:
        skip("not_built", "P25 trunked site-name visibility is unavailable")
    if view.get("mode") != "p25" or not int(view.get("p25.flags", "0")) & 2:
        skip("state", "select a P25 trunked channel before checking site names")
    state = int(view["p25.site_state"])
    name = view["p25.site_name"]
    assert state in range(5), view
    assert len(name) <= 16, view
    assert all(32 <= ord(c) <= 126 or 160 <= ord(c) <= 254 for c in name), view
    if state in (0, 3, 4):
        assert name == "", view
    if state == 2:
        assert int(view["p25.rfss"]) < 255 and int(view["p25.site"]) < 255, view


@pytest.mark.feature("dual_watch")
def test_p25_dual_watch_refused(radio):
    """A stored P25 channel cannot join a debug-link dual-watch pair; no RF or codeplug write."""
    initial = radio.link.view(["core", "features", "p25"])
    if initial.get("mode") != "p25":
        skip("state", "select a stored P25 channel before checking dual-watch admission")
    if not int(initial.get("p25.flags", "0")) & 16:
        skip("state", "use an effectively receive-only P25 channel for the passive admission check")
    if initial.get("dual.built") != "1":
        skip("not_built", "dual watch is disabled")
    count = int(initial.get("zone_size", "0"))
    if count < 2 or initial.get("scan") != "off":
        skip("state", "use a zone with at least two channels and stop scanning first")
    other = (int(initial["zone_pos"]) + 1) % count
    try:
        radio.link.scan("dual", other)
        time.sleep(0.3)  # Allow the queued request to run before checking that it stayed off.
        current = radio.link.view(["core", "features"])
        assert current["scan"] == "off"
        assert current["channel_number"] == initial["channel_number"]
        assert current["last_tx_ms"] == initial["last_tx_ms"]
    finally:
        radio.link.scan("cancel")


@pytest.mark.feature("p25_ui")
def test_p25_talkgroup_knob(radio):
    initial = radio.link.view(["core", "p25", "link"])
    # Restoring the original receive filter uses the trunk-only Listen picker.
    flags = int(initial.get("p25.flags", "0"))
    if initial.get("mode") != "p25" or flags & 18 != 18:
        skip("state", "select an effectively receive-only P25 trunked channel for the knob check")
    count = int(initial.get("p25.groups", "0"))
    if count < 2 or initial.get("p25.listen") not in ("0", "1"):
        skip("state", "configure at least two groups and use Programmed or All listening")
    if initial.get("ui.locked") == "1" or "input.enc_steps" not in initial:
        skip("state", "unlock an encoder-equipped radio before checking the knob")
    index = int(initial["p25.group_index"])
    channel = initial["channel_number"]
    try:
        radio.go_home()
        radio.link.key("enc", DOWN, 1)
        current = radio.wait_for(lambda v: int(v.get("p25.group_index", "-1")) == (index + 1) % count,
                                 sections=("core", "p25"), what="next P25 group")
        assert current["channel_number"] == channel
        assert current["p25.tx_target"] != initial["p25.tx_target"]
        assert current["p25.held_tg"] == current["p25.tx_target"]
        assert current["p25.listen"] == "3"
        assert int(current["p25.flags"]) & 16
        assert current["last_tx_ms"] == initial["last_tx_ms"]
    finally:
        current = radio.link.view(["p25"])
        if current.get("p25.tx_target") != initial["p25.tx_target"]:
            delta = (index - int(current["p25.group_index"])) % count
            if delta > count // 2:
                delta -= count
            radio.link.key("enc", DOWN, delta)
            radio.wait_for(lambda v: v.get("p25.tx_target") == initial["p25.tx_target"],
                           sections=("p25",), what="original P25 target")
        radio.link.key_hold("quick", 250)
        radio.link.wait_keys_idle()
        radio.wait_for(lambda v: v.get("ui.screen") == "quick", sections=("link",), what="P25 Quick Actions")
        radio.link.key_hold("2", 250)
        radio.link.wait_keys_idle()
        radio.wait_for(lambda v: v.get("ui.screen") == "p25-listen", sections=("link",), what="P25 Listen picker")
        radio.link.key_hold("1" if initial["p25.listen"] == "0" else "2", 250)
        radio.link.wait_keys_idle()
        radio.wait_for(lambda v: v.get("p25.listen") == initial["p25.listen"],
                       sections=("p25",), what="original P25 listening mode")
        radio.go_home()


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
        screen("quick")
        if "DMR_TALKGROUP_POOL" in radio.link.version().get("features", "").split(","):
            press("4")
            screen("p25-talkgroups")
            radio.go_home()
            press("quick")
            screen("quick")
        if int(radio.link.view(["p25"]).get("p25.flags", "0")) & 2:
            press("2")
            screen("p25-listen")
            press("back")
            screen("quick")
            press("6")
            screen("p25-recent")
            press("back")
            screen("quick")
        press("7")
        screen("p25-system")
        press("context")
        radio.wait_for(lambda v: v.get("ui.count") == "4", sections=("link",), what="P25 decode details")
        radio.go_home()
        press("context")
        screen("p25-recent")
        radio.go_home()
        press("quick")
        screen("quick")
        press("5")
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
    assert 0 <= int(view["p25.groups"]) <= 63, view
    assert 0 <= int(view["p25.group_index"]) < max(1, int(view["p25.groups"])), view


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

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Read-only visibility check for a preprogrammed conventional P25 channel."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("p25")
def test_p25_selected_owner(radio):
    view = radio.link.view(["core"])
    if "p25.active" not in view:
        skip("not_built", "P25 owner visibility is unavailable")
    if view.get("mode") != "p25":
        skip("state", "select a stored conventional P25 channel before this read-only check")
    assert view["p25.active"] == "1", view
    assert view["p25.rx"] in ("0", "1"), view
    assert view.get("vfo") == "0", view

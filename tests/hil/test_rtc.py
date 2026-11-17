# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Verify that an initialized hardware clock advances. No settings or RF changes."""
import time

import pytest

from rfoundry_hil import skip


@pytest.mark.feature("rtc", optional=True, owner="acquire a GPS fix to initialize the clock")
@pytest.mark.manual
def test_rtc_advances(radio):
    key = "rtc.utc_seconds_plus_one"
    before = radio.view(["link"])
    if key not in before:
        skip("not_built", "no clock sample in the debug link view")
    start = int(before[key])
    assert start > 0, "RTC is unset after GPS initialization"
    t0 = time.monotonic()
    time.sleep(3)
    end = int(radio.view(["link"])[key])
    elapsed = time.monotonic() - t0
    assert end > start and abs(end - start - elapsed) < 2, (start, end, elapsed)

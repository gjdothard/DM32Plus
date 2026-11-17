# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Check that the hardware clock keeps time through power-off."""
import time

import pytest

from rfoundry_hil import DebugLinkError, diag_lines, find, skip

RTC_KEY = "rtc.utc_seconds_plus_one"
POWER_OFF_S = 60
OWNER = "acquire a GPS fix, disable GPS, then use the power knob when prompted"


def _sample(radio):
    view = radio.view(["link"])
    return time.monotonic(), int(view[RTC_KEY]), int(view["uptime_ms"]) / 1000


def _wait_for_power(radio, on):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        try:
            sample = _sample(radio)
        except (DebugLinkError, OSError):
            if not on:
                return time.monotonic()
        else:
            if on:
                return sample
        time.sleep(1)
    pytest.fail(f"the radio did not turn {'on' if on else 'off'} within 45 seconds")


@pytest.mark.feature("rtc", optional=True, owner=OWNER)
@pytest.mark.manual
def test_rtc_retains_time(radio):
    """Use a separate serial cable that stays connected during power-off."""
    before = radio.view(["link"])
    if RTC_KEY not in before:
        skip("not_built", "no clock sample in the debug link view")
    if not (find(diag_lines(radio), "gps ") or "").endswith(" off"):
        skip("state", "disable GPS after the RTC has synchronized")
    start, utc, uptime = _sample(radio)
    assert utc > 0, "RTC is unset after GPS initialization"

    print("Turn the radio fully off now. Keep the serial cable connected.", flush=True)
    powered_off = _wait_for_power(radio, on=False)
    time.sleep(POWER_OFF_S)
    print("Turn the radio back on now.", flush=True)
    end, retained, boot_uptime = _wait_for_power(radio, on=True)

    assert end - boot_uptime > start - uptime, "uptime did not reset"
    assert end - boot_uptime - powered_off >= POWER_OFF_S - 1, "radio came back on too early"
    assert retained > utc, "RTC time was lost or stopped"
    assert abs(retained - utc - (end - start)) < 5, (utc, retained, end - start)

    # The receiver briefly probes at boot before applying the saved GPS setting.
    time.sleep(max(0, 6 - boot_uptime))
    assert (find(diag_lines(radio), "gps ") or "").endswith(" off"), "GPS was enabled after boot"

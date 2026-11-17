// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include <assert.h>
#include <stdio.h>
#include "app/wall_clock.h"
#include "board.h"
#include "hal_time.h"
#include "ui/ui_svc.h"
#include "dmr/dmrDataService.h"

static uint32_t now, rtc_value, gps_value;
static bool rtc_valid, gps_valid, reject_write;
static unsigned writes;

uint32_t hal_millis(void) { return now; }

static bool read_rtc(uint32_t *utc)
{
	if (!rtc_valid)
		return false;
	*utc = rtc_value;
	return true;
}

static bool write_rtc(uint32_t utc)
{
	writes++;
	if (reject_write)
		return false;
	rtc_value = utc;
	rtc_valid = true;
	return true;
}

static const hal_rtc_t rtc = { .read = read_rtc, .write = write_rtc };
const board_t board = { .rtc = &rtc };

bool aprs_gps_rtc_utc(uint32_t ms, uint32_t *utc)
{
	(void)ms;
	*utc = gps_value;
	return gps_valid;
}

uint16_t aprs_utc_minute(uint32_t ms)
{
	(void)ms;
	return gps_valid ? (uint16_t)(gps_value / 60u % 1440u + 1u) : 0;
}

static void check_minute(uint16_t expected)
{
	assert(ui_svc_utc_minute() == expected);
	assert(dmrDataServiceUtcMinute(now) == expected);
}

int main(void)
{
	uint32_t utc;
	assert(wall_clock_from_rmc(10120, 0, &utc) && utc == 0);
	assert(wall_clock_from_rmc(290224, 235959, &utc));
	assert(!wall_clock_from_rmc(290223, 0, &utc));
	assert(!wall_clock_from_rmc(310426, 0, &utc));
	assert(!wall_clock_from_rmc(10126, UINT32_MAX, &utc));
	assert(!wall_clock_from_rmc(10126, 240000, &utc));

	// An unset clock stays unknown. A retained clock rolls over without GPS.
	wall_clock_poll();
	check_minute(0);
	rtc_valid = true;
	rtc_value = 86399u;
	now += 1000;
	wall_clock_poll();
	check_minute(1440);
	rtc_value++;
	now += 1000;
	wall_clock_poll();
	check_minute(1);
	assert(writes == 0);

	// Reacquiring GPS corrects the RTC. Losing GPS keeps the corrected time.
	gps_valid = true;
	gps_value = rtc_value + 90000u;
	check_minute(61);
	now += 1000;
	wall_clock_poll();
	assert(writes == 1 && rtc_value == gps_value);
	gps_valid = false;
	check_minute(61);

	// A failed read stops advertising time.
	rtc_valid = false;
	now += 1000;
	wall_clock_poll();
	check_minute(0);

	// A failed write keeps GPS usable and is retried only after the cooldown.
	gps_valid = true;
	reject_write = true;
	now += 30000;
	wall_clock_poll();
	check_minute(61);
	assert(writes == 2 && wall_clock_rtc_seconds() == 0);
	now += 1000;
	wall_clock_poll();
	assert(writes == 2);
	reject_write = false;
	now += 29000;
	wall_clock_poll();
	assert(writes == 3 && rtc_value == gps_value);
	gps_valid = false;
	check_minute(61);

	puts("wall clock passed");
	return 0;
}

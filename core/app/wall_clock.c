// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include "config.h"
#if CONFIG_RTC
#include "app/wall_clock.h"
#include "board.h"
#include "hal_time.h"
#include "ui/ui_svc.h"
#if CONFIG_APRS && CONFIG_GPS
#include "aprs/aprs.h"
#endif
#if CONFIG_DMR_SMS
#include "dmr/dmrDataService.h"
#endif

#define POLL_MS 1000u
#define SYNC_TOLERANCE_S 2u

// Only housekeeping writes this aligned word. Readers never wait for I2C.
static volatile uint32_t rtc_seconds;	// seconds plus one, or zero if unavailable

bool wall_clock_from_rmc(uint32_t date, uint32_t hms, uint32_t *utc)
{
	static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t year = date % 100u;
	uint32_t month = date / 100u % 100u;
	uint32_t day = date / 10000u;
	uint32_t hour = hms / 10000u;
	uint32_t minute = hms / 100u % 100u;
	uint32_t second = hms % 100u;
	if (year < 20u || month == 0 || month > 12u || day == 0 || hour >= 24u || minute >= 60u || second >= 60u)
		return false;
	if (day > month_days[month - 1u] + (uint32_t)(month == 2u && year % 4u == 0))
		return false;

	// 2020 is a leap year. The two-digit year is restricted to 2020–2099.
	uint32_t years = year - 20u;
	uint32_t days = years * 365u + (years + 3u) / 4u + day - 1u;
	for (uint32_t m = 1; m < month; m++)
		days += month_days[m - 1u] + (uint32_t)(m == 2u && year % 4u == 0);
	*utc = days * RTC_DAY_SECONDS + hour * 3600u + minute * 60u + second;
	return true;
}

void wall_clock_poll(void)
{
	static bool polled;
	static uint32_t last_poll;
	if (!board.rtc)
		return;

	uint32_t now = hal_millis();
	if (polled && now - last_poll < POLL_MS)
		return;
	polled = true;
	last_poll = now;
	uint32_t utc = 0;
	bool valid = board.rtc->read(&utc);

#if CONFIG_APRS && CONFIG_GPS
	static bool attempted_sync;
	static uint32_t last_sync;
	uint32_t gps;
	if ((!attempted_sync || now - last_sync >= 30000u) && aprs_gps_rtc_utc(hal_millis(), &gps)) {
		uint32_t error = gps > utc ? gps - utc : utc - gps;
		if (!valid || error > SYNC_TOLERANCE_S) {
			attempted_sync = true;
			last_sync = now;
			valid = board.rtc->write(gps) && board.rtc->read(&utc);
		}
	}
#endif
	rtc_seconds = valid ? utc + 1u : 0;
}

uint32_t wall_clock_rtc_seconds(void)
{
	return rtc_seconds;
}

static uint16_t current_minute(uint32_t now)
{
#if CONFIG_APRS && CONFIG_GPS
	uint16_t gps = aprs_utc_minute(now);
	if (gps)
		return gps;
#else
	(void)now;
#endif
	uint32_t rtc = wall_clock_rtc_seconds();
	return rtc ? (uint16_t)(((rtc - 1u) / 60u) % 1440u + 1u) : 0;
}

uint16_t ui_svc_utc_minute(void)
{
	return current_minute(hal_millis());
}

#if CONFIG_DMR_SMS
uint16_t dmrDataServiceUtcMinute(uint32_t now)
{
	return current_minute(now);
}
#endif
#endif

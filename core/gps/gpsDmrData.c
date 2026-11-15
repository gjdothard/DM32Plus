// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Adapt the board's GPS snapshot, shared with the Position screen, for LRRP.
#include "aprs/aprs.h"
#include "dmr/dmrDataService.h"
#include "hal_time.h"

static void split_coordinate(int32_t value, uint8_t *degrees, uint32_t *minutes10000)
{
	uint32_t magnitude = value < 0 ? -(int64_t)value : value;
	// Round to the nearest ten-thousandth of a minute before splitting at the degree boundary.
	uint32_t total = (magnitude / 50u) * 3u + ((magnitude % 50u) * 3u + 25u) / 50u;
	*degrees = total / 600000u;
	*minutes10000 = total % 600000u;
}

bool dmrDataServiceGetPosition(lrrpPosition_t *position)
{
	aprs_gps_t gps;
	if (!aprs_gps(&gps, hal_millis()) || !gps.fix)
		return false;

	*position = (lrrpPosition_t){0};
	dmrDataLocation_t *loc = &position->location;
	loc->fixValid = true;
	loc->north = gps.lat >= 0;
	loc->east = gps.lon >= 0;
	split_coordinate(gps.lat, &loc->latDegrees, &loc->latMinutes10000);
	split_coordinate(gps.lon, &loc->lonDegrees, &loc->lonMinutes10000);
	loc->speedKnots = gps.speed_kmh == UINT16_MAX ? DMR_LOCATION_SPEED_UNKNOWN :
			  (gps.speed_kmh * 1000u + 926u) / 1852u;
	position->hasCourse = gps.course != UINT16_MAX;
	position->courseDegrees = gps.course;
	if (gps.date && gps.utc_hms != UINT32_MAX) {
		position->utcYear = 2000 + gps.date % 100u;
		position->utcMonth = gps.date / 100u % 100u;
		position->utcDay = gps.date / 10000u;
		loc->utcHours = gps.utc_hms / 10000u;
		loc->utcMinutes = gps.utc_hms / 100u % 100u;
		loc->utcSeconds = gps.utc_hms % 100u;
	}
	return true;
}

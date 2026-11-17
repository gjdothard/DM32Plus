// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef WALL_CLOCK_H
#define WALL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool wall_clock_from_rmc(uint32_t date, uint32_t hms, uint32_t *utc);
void wall_clock_poll(void);
uint32_t wall_clock_rtc_seconds(void);	// seconds since 2020-01-01 UTC plus one, zero if unavailable

#endif

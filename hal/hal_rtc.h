// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef HAL_RTC_H
#define HAL_RTC_H

#include <stdbool.h>
#include <stdint.h>

#define RTC_DAY_SECONDS 86400u
#define RTC_DAYS 29220u	// 2020 through 2099 inclusive

// Seconds since 2020-01-01 UTC. Read fails if the clock is unset or cannot be read.
// Both operations are called by the startup and housekeeping task only.
typedef struct {
	bool (*read)(uint32_t *utc);
	bool (*write)(uint32_t utc);
} hal_rtc_t;

#endif

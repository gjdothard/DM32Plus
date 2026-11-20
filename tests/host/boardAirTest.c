// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// A board may omit the new providers. The real session must reject absence,
// unsupported profiles and a busy provider, and release an acquired provider once.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "air/air_session.h"

static int checks, failures, opens, closes, restarts;
static uint32_t offered;
static bool available;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

static uint32_t fit(const air_profile_t *p)
{
	return p->sym_rate == 4800 && p->levels == 4 ? offered : 0;
}

static bool open_provider(const air_profile_t *p, air_event_cb_t cb, void *ctx)
{
	(void)p;
	(void)cb;
	(void)ctx;
	opens++;
	return available;
}

static void close_provider(void) { closes++; }
static void restart_provider(void) { restarts++; }

static const hal_air_t provider = {
	.fit = fit, .open = open_provider, .close = close_provider, .rx_restart = restart_provider
};
static const hal_baseband_t tap = { .rx_rate_hz = 8000, .rx_channels = 1 };
static const board_t legacy_board = { 0 };
static const board_t provided_board = { .air = &provider, .baseband = &tap };
static const air_mode_t mode = {
	.profile = { .sym_rate = 4800, .levels = 4, .shape = AIR_SHAPE_C4FM }, .lost_frames = 1
};
static const air_session_ops_t ops = { 0 };
static uint8_t hard[32];

static bool begin(air_session_t *s, const board_t *b, const air_mode_t *m)
{
	return air_session_open(s, b->air, m, NULL, hard, NULL, sizeof hard * 4, &ops, NULL);
}

int main(void)
{
	air_session_t s;
	memset(&s, 0, sizeof s);
	CHECK(legacy_board.air == NULL && legacy_board.baseband == NULL);
	CHECK(!begin(&s, &legacy_board, &mode));
	CHECK(!air_session_is_open(&s) && opens == 0);
	air_session_close(&s);
	CHECK(closes == 0);

	CHECK(provided_board.baseband->rx_rate_hz == 8000);
	offered = 0;
	CHECK(!begin(&s, &provided_board, &mode) && opens == 0);
	offered = AIR_FIT_RX;
	air_mode_t unsupported = mode;
	unsupported.profile.sym_rate = 9600;
	CHECK(!begin(&s, &provided_board, &unsupported) && opens == 0);
	available = false;
	CHECK(!begin(&s, &provided_board, &mode) && opens == 1);
	CHECK(!air_session_is_open(&s));
	air_session_close(&s);
	CHECK(closes == 0);

	available = true;
	CHECK(begin(&s, &provided_board, &mode) && opens == 2);
	CHECK(air_session_is_open(&s) && s.fit == AIR_FIT_RX);
	CHECK(!air_session_tx_prime(&s));
	CHECK(!air_session_tx_go(&s));
	air_session_retuned(&s);
	CHECK(restarts == 1);
	air_session_close(&s);
	CHECK(!air_session_is_open(&s) && closes == 1);
	air_session_close(&s);
	air_session_retuned(&s);
	CHECK(closes == 1 && restarts == 1);

	// Provider TX capability is retained; channel/system policy will decide admission
	offered = AIR_FIT_RX | AIR_FIT_TX;
	CHECK(begin(&s, &provided_board, &mode));
	CHECK(s.fit == (AIR_FIT_RX | AIR_FIT_TX));
	air_session_close(&s);
	CHECK(closes == 2);
	printf("boardAirTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

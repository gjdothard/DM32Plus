// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// A bounded, published receive view; no pointers into the decoder's loan.
#ifndef P25_MONITOR_H
#define P25_MONITOR_H
#include <stdint.h>
#include <stdbool.h>

#define P25_MONITOR_RECENT 4
#define P25_MONITOR_FAVORITES 4
enum { P25_LISTEN_PROGRAMMED, P25_LISTEN_ALL, P25_LISTEN_FAVORITES, P25_LISTEN_HOLD };
enum { P25_MON_ALL, P25_MON_FAVORITES, P25_MON_HOLD, P25_MON_ADD, P25_MON_REMOVE, P25_MON_PROGRAMMED, P25_MON_SELECT, P25_MON_STEP };
#define P25_MON_ACTIVE 1u
#define P25_MON_TRUNK 2u
#define P25_MON_RX 4u
#define P25_MON_ENC 8u
#define P25_MON_RX_ONLY 16u
#define P25_MON_UNIT 32u
#define P25_MON_TX_UNIT 64u

typedef struct {
	uint32_t src, hz, ms;
	uint16_t tg;
	uint8_t flags, reserved;
} p25_monitor_call_t;

typedef struct {
	uint32_t tuned_hz, cc_hz, wacn, blocks, crc_fail, tx_target;
	uint32_t base_generation, settings_generation;
	uint16_t channel, sysid, nac, held_tg;
	uint16_t favorites[P25_MONITOR_FAVORITES];
	uint8_t flags, state, rfss, site, refused, listen, recent_count, favorite_count;
	uint8_t system;
	p25_monitor_call_t recent[P25_MONITOR_RECENT];
} p25_monitor_t;

// Successful copies are coherent; false leaves a cleared result.
bool radio_p25_monitor(p25_monitor_t *out);
// Radio-task only. SELECT changes the target to an assigned group; monitoring commands never change TX.
bool radio_p25_monitor_command(uint8_t op, uint16_t tg);
// Radio-task only: assigned group count and the current target's position.
uint8_t radio_p25_talkgroups(uint8_t *index);
#endif

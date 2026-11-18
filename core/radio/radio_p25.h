// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// Conventional P25 mode ownership, adapted from the pinned RadioFoundry radio_p25 implementation.
#ifndef RADIO_P25_H
#define RADIO_P25_H

#include "config.h"
#if CONFIG_P25
#include "codeplug/codeplug.h"
#include "p25/p25_workspace.h"

// Owner storage, exposed for compile-only resource accounting. Callers must not access a running owner's state.
typedef struct {
	p25_workspace_t work;
	channel_t channel;
	uint32_t base_generation, settings_generation;
	uint16_t number;
} __attribute__((may_alias)) radio_p25_mem_t;

// Caller task has stopped DMR/AMBE and tuned the board in digital mode. Stored conventional channels only.
// This module never keys the radio. The state machine performs its normal checks; teardown and failed go unkey.
bool radio_p25_begin(const channel_t *channel, uint16_t number);
// Close air, stop audio, then return the codec loan. False retains a failed return for a later retry.
bool radio_p25_end(void);
bool radio_p25_running(void);
bool radio_p25_params(const channel_t *channel, uint16_t number, p25_engine_cfg_t *cfg);
// Tick on the owner task. A changed stored configuration closes the session; false means it is no longer running.
bool radio_p25_poll(p25_engine_view_t *view);
// Call before keying, then go only after the caller's RF checks/keying. Go failure requires immediate unkey.
bool radio_p25_tx_prepare(void);
bool radio_p25_tx_go(void);
void radio_p25_tx_stop(void);
void radio_p25_tx_abort(void);
uint8_t radio_p25_tx_state(void);
void radio_p25_tx_clear(void);
#endif
#endif

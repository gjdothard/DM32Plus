// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// P25 mode ownership, adapted from the pinned RadioFoundry radio_p25 implementation.
#ifndef RADIO_P25_H
#define RADIO_P25_H

#include "config.h"
#if CONFIG_P25
#include "codeplug/codeplug.h"
#include "p25/p25_workspace.h"

// Owner storage, exposed for compile-only resource accounting. Callers must not access a running owner's state.
#if CONFIG_P25_TRUNK
typedef bool (*radio_p25_key_t)(uint32_t hz, bool automatic, void *ctx);
typedef struct {
	p25_follow_cfg_t cfg;
	p25_follow_cc_t cc[P25_CP_CTL_MAX];
	p25_follow_iden_t iden[P25_FOLLOW_IDENS];
	radio_p25_key_t key;
	void *key_ctx;
	volatile uint8_t in_prov, in_radio, want, done;
	uint32_t hz, id, tx_hz;
	uint8_t what, burst_permission, burst_tune, tx_kind;
	uint8_t burst[P25_TSBK_BYTES];
	uint32_t burst_hz;
	bool burst_pending, ptt, ptt_pending, talk_end, tuned;
} radio_p25_trunk_t;
#endif
typedef struct {
	p25_workspace_t work;
	channel_t channel;
	uint32_t base_generation, settings_generation;
	uint16_t number;
#if CONFIG_P25_TRUNK
	radio_p25_trunk_t trunk;
#endif
} __attribute__((may_alias)) radio_p25_mem_t;

// Caller task has stopped DMR/AMBE and tuned the board in digital mode. Stored channels only.
// This module never keys the radio. The state machine performs its normal checks; teardown and failed go unkey.
bool radio_p25_begin(const channel_t *channel, uint16_t number);
// Close air, stop audio, then return the codec loan. False retains a failed return for a later retry.
bool radio_p25_end(void);
bool radio_p25_running(void);
#if CONFIG_DIAG
typedef struct {
	uint32_t chunks, symbols, tuned_hz, blocks, crc_fail;
	uint16_t syncs, locks, rejects, frames;
	bool active, tuned;
} radio_p25_diag_t;
// Radio-task-published snapshot; no owner workspace is dereferenced by the caller.
bool radio_p25_diag(radio_p25_diag_t *out);
#endif
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
#if CONFIG_P25_TRUNK
bool radio_p25_trunked(void);
// Attach the radio task's normal admission/keying callback before polling. No callback means no emissions.
void radio_p25_trunk_keyer(radio_p25_key_t key, void *ctx);
bool radio_p25_trunk_ptt(bool down);
void radio_p25_trunk_view(p25_follow_view_t *out);
uint32_t radio_p25_tx_frequency(void);
bool radio_p25_tx_automatic(void);
bool radio_p25_trunk_failed(void);
// Radio-task snapshot into the published view. If the provider is inside the follower, retain the previous values.
void radio_p25_trunk_status(uint8_t *state, uint8_t *registration, uint8_t *request);
#endif
#endif
#endif

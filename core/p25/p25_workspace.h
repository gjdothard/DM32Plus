// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef P25_WORKSPACE_H
#define P25_WORKSPACE_H

#include "config.h"
#if CONFIG_P25
#include "hal_vocoder.h"
#include "p25Voice.h"
#include "p25_engine.h"
#if CONFIG_P25_TRUNK
#include "p25_follow.h"
#endif

// The owner stops AMBE before acquiring and stops P25 audio/air callbacks before releasing.
// May alias the idle codec's typed scratch storage; no second permanent allocation is needed.
typedef struct {
	p25_voice_mem_t voice;
	p25_engine_t engine;
#if CONFIG_P25_TRUNK
	p25_follow_t follower;
#endif
} __attribute__((may_alias)) p25_workspace_t;

typedef struct {
	const hal_vocoder_t *vocoder;
	hal_vocoder_loan_t loan;
} p25_workspace_lease_t;

// Initialize lease to zero. Acquisition clears the workspace. A malformed provider loan whose cleanup fails stays
// in lease for a later release; other failures preserve the lease. Check success before using its data.
bool p25_workspace_acquire(p25_workspace_lease_t *lease, const hal_vocoder_t *vocoder);
bool p25_workspace_release(p25_workspace_lease_t *lease);

#endif
#endif

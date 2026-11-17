// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef SIM_RADIO_TASK_H
#define SIM_RADIO_TASK_H

#include <stddef.h>
#include "radio/radio_sm.h"

// Model the task's storage step on a fake board with no saved default key.
// Explicit encrypted-channel policies stay pending for their own tests.
static inline void sim_radio_resolve_default(radio_sm_t *sm)
{
#if CONFIG_DMR_AES256
	if (sm->aes_pending && sm->view.ch.mode == RADIO_MODE_DMR && sm->view.ch.privacy_slot == 0)
		radio_sm_aes_resolved(sm, NULL, 0, 0, true);
#else
	(void)sm;
#endif
}

#endif

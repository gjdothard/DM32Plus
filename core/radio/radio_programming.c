// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include "radio/radio_programming.h"
#if CONFIG_CPS_FAST
static volatile unsigned char hold;
bool radio_programming_acquire(void)
{
	if (!hold) hold = 1;
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	return hold == 2;
}
bool radio_programming_requested(void) { return hold != 0; }
void radio_programming_ready(void) { __atomic_signal_fence(__ATOMIC_SEQ_CST); if (hold) hold = 2; }
void radio_programming_release(void) { __atomic_signal_fence(__ATOMIC_SEQ_CST); hold = 0; }
#endif

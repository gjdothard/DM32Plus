// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef RADIO_PROGRAMMING_H
#define RADIO_PROGRAMMING_H
#include <stdbool.h>
#include "config.h"
#if CONFIG_CPS_FAST
// Host task requests a hold; only the radio task acknowledges after stopping every RF owner.
bool radio_programming_acquire(void);
bool radio_programming_requested(void);
void radio_programming_ready(void);
void radio_programming_release(void);
#endif
#endif

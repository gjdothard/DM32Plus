// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Link the parameter-only tests to real storage dependencies without accessing a flash device.
#include "board.h"
const board_t board = {0};
void storage_lock(void) {}
void storage_unlock(void) {}
bool ambeVoiceStreaming(void) { return false; }
bool radio_am_tx_refused(uint32_t hz) { (void)hz; return false; }
bool radio_bcast_band(uint32_t hz) { (void)hz; return false; }

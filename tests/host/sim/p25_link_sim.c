// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Link the parameter-only tests to real storage dependencies without accessing a flash device.
#include "board.h"
const board_t board = {0};
void storage_lock(void) {}
void storage_unlock(void) {}

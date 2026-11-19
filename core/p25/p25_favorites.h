// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef P25_FAVORITES_H
#define P25_FAVORITES_H
#include "codeplug/codeplug.h"
#include "radio/p25_monitor.h"
// Caller holds storage_lock. Favorites are scoped to the system's name and declared identity.
bool p25_favorites_read(const channel_t *ch, uint16_t out[P25_MONITOR_FAVORITES], uint8_t *count);
bool p25_favorites_edit(const channel_t *ch, uint16_t tg, bool remove);
#endif

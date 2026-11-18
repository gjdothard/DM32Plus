// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef RFOUNDRY_P25_ADMISSION_H
#define RFOUNDRY_P25_ADMISSION_H

#include "codeplug/codeplug.h"

enum {
	P25_TX_VOICE = 1u,
	P25_TX_REQUEST = 2u,
	P25_TX_REGISTRATION = 4u,
};

#if CONFIG_P25
// Equality of P25-relevant channel fields; ignores struct padding and unrelated DMR settings.
bool p25_channel_matches(const channel_t *a, const channel_t *b);
// Stored-channel permissions for the active configuration. Zero on stale, absent, unsupported or malformed records.
// No cached result authorizes RF: the owner must cancel queued work on settings changes and check the current
// session, grant, frequency, band and normal radio protections before every emission. NO_REG clears registration
// permission only; RX-only and system listen-only clear every permission. No VFO/settings overrides are admitted.
uint8_t p25_codeplug_tx_permissions(uint16_t number, const channel_t *active);
#endif

#endif

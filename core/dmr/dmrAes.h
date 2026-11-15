// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AES-256 voice privacy, independent of a board or modem (docs/spec/dmr-voice.md).
#ifndef RFOUNDRY_DMR_AES_H
#define RFOUNDRY_DMR_AES_H

#include "dmr/call/dmr_call.h"

typedef struct {
	uint8_t key[32], tx_stream[126], rx_stream[126];
	uint32_t tx_next, rx_mi, rx_pending, rx_slot, remaining;
	uint32_t tx_le[3], rx_le[3];
	uint8_t key_id, tx_last, rx_last, rx_mask;
	bool loaded, rx_valid, rx_rejected, rx_have_index, rx_have_pending;
} dmr_aes_t;

// Session only: the host supplies a nonzero MI seed; keys and MI state are never persisted or exposed in views.
bool dmrAesLoad(dmr_aes_t *c, const uint8_t key[32], uint8_t key_id, uint32_t seed);
void dmrAesClear(dmr_aes_t *c);
uint32_t dmrAesAdvance(uint32_t mi);
void dmrAesBlock(const uint8_t key[32], const uint8_t in[16], uint8_t out[16]);
void dmrAesStream(const uint8_t key[32], uint32_t mi, uint8_t out[126]);
extern const dmr_privacy_ops_t dmr_aes_ops;

#endif

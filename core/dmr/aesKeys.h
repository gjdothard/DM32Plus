// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef RFOUNDRY_AES_KEYS_H
#define RFOUNDRY_AES_KEYS_H
#include <stdbool.h>
#include <stdint.h>
#define AES_KEY_SLOTS 4u
// Public metadata only. Keys never appear in codeplug exports or metadata replies.
bool aesKeysList(uint8_t ids[AES_KEY_SLOTS], uint8_t *active);
bool aesKeysPut(uint8_t slot, uint8_t id, const uint8_t key[32], uint32_t seed);
bool aesKeysDelete(uint8_t slot);
// Reserve a disjoint 65,536-superframe MI range durably before returning key material. Slot 0 disables AES.
bool aesKeysSelect(uint8_t slot, uint8_t key[32], uint8_t *id, uint32_t *seed);
// Reserve for a channel without changing the radio-wide default.
bool aesKeysAcquire(uint8_t slot, uint8_t key[32], uint8_t *id, uint32_t *seed);
uint32_t aesKeysSerial(void);
// 1 restored, 0 no selected key/storage, -1 unsafe or failed restoration: caller must inhibit TX.
int aesKeysRestore(uint8_t key[32], uint8_t *id, uint32_t *seed);
bool aesKeysClear(void);
#endif

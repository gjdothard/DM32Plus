// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Append-only key bank in the last two sectors of the reserved native area. No key bank lives in RAM.
#include "config.h"
#if CONFIG_DMR_AES256
#include <string.h>
#include "dmr/aesKeys.h"
#include "dmr/dmrAes.h"
#include "storage/storage.h"

#define MAGIC 0x59454B41u
#define RECORD 256u
#define COUNT (2u * STORAGE_SECTOR / RECORD)
#define SLOT_OFFSET 16u
#define SLOT_BYTES 44u
#define CRC_OFFSET 248u
static uint32_t changes;

uint32_t aesKeysSerial(void) { return changes; }

typedef struct {
	uint8_t *record, *scratch;
	uint32_t base, sequence;
	int latest, free;
} bank_t;

static void wipe(void *ptr, unsigned n)
{
	volatile uint8_t *p = ptr;
	while (n--)
		*p++ = 0;
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t n)
{
	for (unsigned i = 0; i < 4; i++)
		p[i] = (uint8_t)(n >> (8 * i));
}

static bool blank(const uint8_t *p)
{
	for (unsigned i = 0; i < RECORD; i++)
		if (p[i] != 255)
			return false;
	return true;
}

static void close_bank(bank_t *b)
{
	if (b->record) {
		wipe(b->scratch, 2 * RECORD);
		storage_buffer_release();
	}
	storage_unlock();
}

static bool open_bank(bank_t *b)
{
	memset(b, 0, sizeof(*b));
	b->latest = b->free = -1;
	storage_lock();
	const hal_flash_map_t *m = storage_map();
	if (!storage_ready() || !m || m->native_size < 2 * STORAGE_SECTOR ||
	    m->native_addr % STORAGE_SECTOR || m->native_size % STORAGE_SECTOR ||
	    m->native_addr > board.flash->size || m->native_size > board.flash->size - m->native_addr)
		goto fail;
	b->base = m->native_addr + m->native_size - 2 * STORAGE_SECTOR;
	b->scratch = storage_buffer_acquire();
	if (!b->scratch)
		goto fail;
	b->record = b->scratch + RECORD;
	memset(b->record, 0, RECORD);
	b->record[8] = 1;
	bool unknown = false;
	bool damaged = false;
	uint32_t damaged_sequence = 0;
	for (unsigned n = 0; n < COUNT; n++) {
		uint8_t *r = b->scratch;
		if (!board.flash->read(b->base + n * RECORD, r, RECORD))
			goto fail;
		if (blank(r)) {
			if (b->free < 0)
				b->free = (int)n;
			continue;
		}
		// A torn erase can damage old records. A complete bank in the other sector establishes ownership.
		if (get32(r) != MAGIC) {
			unknown = true;
			if (!get32(r + 252)) {
				uint32_t seq = get32(r + 4);
				if (!damaged || (int32_t)(seq - damaged_sequence) > 0)
					damaged_sequence = seq;
				damaged = true;
			}
			continue;
		}
		if (get32(r + 252))
			continue;
		if (get32(r + CRC_OFFSET) != storage_crc32(0, r, CRC_OFFSET)) {
			uint32_t seq = get32(r + 4);
			if (!damaged || (int32_t)(seq - damaged_sequence) > 0)
				damaged_sequence = seq;
			damaged = true;
			continue;
		}
		if (r[8] != 1)
			goto fail;	// a complete bank from a later format must not be overwritten
		uint32_t seq = get32(r + 4);
		if (b->latest < 0 || (int32_t)(seq - b->sequence) > 0) {
			b->latest = (int)n;
			b->sequence = seq;
			memcpy(b->record, r, RECORD);
		}
	}
	if (unknown && b->latest < 0)
		goto fail;
	if (damaged && (b->latest < 0 || (int32_t)(damaged_sequence - b->sequence) > 0))
		goto fail;	// do not roll MI allocation back past a damaged, potentially used reservation
	return true;
fail:
	close_bank(b);
	return false;
}

static bool save(bank_t *b)
{
	if (b->free < 0) {
		// Only erase the sector that does not contain the latest committed bank.
		unsigned sector = b->latest >= (int)(COUNT / 2) ? 0u : 1u;
		if (!board.flash->erase(b->base + sector * STORAGE_SECTOR))
			return false;
		b->free = (int)(sector * COUNT / 2);
	}
	uint8_t *r = b->record;
	put32(r, MAGIC);
	put32(r + 4, b->sequence + 1);
	put32(r + CRC_OFFSET, storage_crc32(0, r, CRC_OFFSET));
	memset(r + 252, 255, 4);
	uint32_t addr = b->base + (uint32_t)b->free * RECORD;
	if (!board.flash->write(addr, r, 252))
		return false;
	memset(r + 252, 0, 4);
	if (!board.flash->write(addr + 252, r + 252, 4) || !board.flash->read(addr, b->scratch, RECORD))
		return false;
	return memcmp(r, b->scratch, RECORD) == 0;
}

static uint8_t *slot(bank_t *b, unsigned n)
{
	return b->record + SLOT_OFFSET + (n - 1) * SLOT_BYTES;
}

bool aesKeysList(uint8_t ids[AES_KEY_SLOTS], uint8_t *active)
{
	memset(ids, 0, AES_KEY_SLOTS);
	*active = 0;
	bank_t b;
	if (!open_bank(&b))
		return false;
	for (unsigned n = 1; n <= AES_KEY_SLOTS; n++)
		ids[n - 1] = slot(&b, n)[0];
	*active = b.record[9];
	close_bank(&b);
	return true;
}

bool aesKeysPut(uint8_t n, uint8_t id, const uint8_t key[32], uint32_t seed)
{
	if (!n || n > AES_KEY_SLOTS || !id || !key || !seed)
		return false;
	bank_t b;
	if (!open_bank(&b))
		return false;
	bool ok = false;
	for (unsigned i = 1; i <= AES_KEY_SLOTS; i++) {
		uint8_t *s = slot(&b, i);
		if (i != n && s[0] && (s[0] == id || !memcmp(s + 4, key, 32)))
			goto done;
	}
	uint8_t *s = slot(&b, n);
	if (!s[0] || memcmp(s + 4, key, 32)) {
		memset(s, 0, SLOT_BYTES);
		memcpy(s + 4, key, 32);
		put32(s + 36, seed);
		put32(s + 40, 0xFFFF0000u);
	}
	s[0] = id;
	// A replacement takes effect only when explicitly selected or on the next boot.
	ok = save(&b);
	if (ok)
		changes++;
done:
	close_bank(&b);
	return ok;
}

// Jump 65,536 superframes without a long blocking bit-shift loop. This is the linear MI advance raised to 2^16.
static uint32_t jump(uint32_t mi)
{
	uint32_t basis[32], square[32];
	for (unsigned i = 0; i < 32; i++)
		basis[i] = dmrAesAdvance(1u << i);
	for (unsigned power = 0; power < 16; power++) {
		for (unsigned i = 0; i < 32; i++) {
			uint32_t v = basis[i], out = 0;
			for (unsigned j = 0; j < 32; j++)
				if ((v >> j) & 1u)
					out ^= basis[j];
			square[i] = out;
		}
		memcpy(basis, square, sizeof(basis));
	}
	uint32_t out = 0;
	for (unsigned i = 0; i < 32; i++)
		if ((mi >> i) & 1u)
			out ^= basis[i];
	return out;
}

static bool select_bank(bank_t *b, uint8_t n, uint8_t key[32], uint8_t *id, uint32_t *seed, bool select)
{
	*id = 0;
	*seed = 0;
	if (n > AES_KEY_SLOTS)
		return false;
	if (n) {
		uint8_t *s = slot(b, n);
		if (!s[0] || get32(s + 40) < 65536u || !get32(s + 36))
			return false;
		*id = s[0];
		*seed = get32(s + 36);
		put32(s + 36, jump(*seed));
		put32(s + 40, get32(s + 40) - 65536u);
	}
	if (select)
		b->record[9] = n;
	if (!save(b)) {
		*id = 0;
		*seed = 0;
		return false;
	}
	if (n)
		memcpy(key, slot(b, n) + 4, 32);
	return true;
}

bool aesKeysSelect(uint8_t n, uint8_t key[32], uint8_t *id, uint32_t *seed)
{
	bank_t b;
	if (!open_bank(&b))
		return false;
	bool ok = select_bank(&b, n, key, id, seed, true);
	close_bank(&b);
	return ok;
}

bool aesKeysAcquire(uint8_t n, uint8_t key[32], uint8_t *id, uint32_t *seed)
{
	if (!n || n > AES_KEY_SLOTS)
		return false;
	bank_t b;
	if (!open_bank(&b))
		return false;
	bool ok = select_bank(&b, n, key, id, seed, false);
	close_bank(&b);
	return ok;
}

int aesKeysRestore(uint8_t key[32], uint8_t *id, uint32_t *seed)
{
	const hal_flash_map_t *m = storage_map();
	if (!storage_ready() || !m || m->native_size < 2 * STORAGE_SECTOR)
		return 0;
	bank_t b;
	if (!open_bank(&b))
		return -1;
	int result = 0;
	if (b.record[9])
		result = select_bank(&b, b.record[9], key, id, seed, false) ? 1 : -1;
	close_bank(&b);
	return result;
}

bool aesKeysDelete(uint8_t n)
{
	if (!n || n > AES_KEY_SLOTS)
		return false;
	bank_t b;
	if (!open_bank(&b))
		return false;
	memset(slot(&b, n), 0, SLOT_BYTES);
	if (b.record[9] == n)
		b.record[9] = 0;
	bool ok = save(&b);
	if (ok)
		changes++;
	close_bank(&b);
	return ok;
}

bool aesKeysClear(void)
{
	bank_t b;
	if (!open_bank(&b))
		return false;
	memset(b.record + 9, 0, CRC_OFFSET - 9);
	bool ok = save(&b);
	if (ok)
		changes++;
	close_bank(&b);
	return ok;
}
#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include "config.h"

#if CONFIG_MESSAGE_JOURNAL
#include <string.h>
#include "storage/message_journal.h"
#include "storage/storage.h"

#define MAGIC		0x31474D52u	// RMG1
#define HEADER_SIZE	16u
#define HEADER_CRC	8u
#define TEXT_OFFSET	17u
#define RECORD_CRC	(TEXT_OFFSET + MESSAGE_JOURNAL_TEXT)
#define SECTOR		STORAGE_SECTOR
#define SLOT		MESSAGE_JOURNAL_SCRATCH
#define SLOTS		((SECTOR - HEADER_SIZE) / SLOT)
#define MAX_GENERATION	(UINT32_MAX / SLOTS - 1u)

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
	for (unsigned i = 0; i < 4; i++) {
		p[i] = (uint8_t)(v >> (8u * i));
	}
}

static uint32_t sector_addr(const message_journal_t *j, uint32_t generation)
{
	return j->base + ((generation - 1u) % j->sectors) * SECTOR;
}

static bool sector_header(const message_journal_t *j, uint32_t addr, uint32_t *generation)
{
	uint8_t h[HEADER_SIZE];
	if (!j->flash->read(addr, h, sizeof(h))) {
		return false;
	}
	uint32_t g = get32(h + 4);
	*generation = 0;
	if (g != 0 && g <= MAX_GENERATION && sector_addr(j, g) == addr && get32(h) == MAGIC &&
	    h[HEADER_SIZE - 1u] == 0 && get32(h + HEADER_CRC) == storage_crc32(0, h, HEADER_CRC)) {
		*generation = g;
	}
	return true;
}

bool message_journal_mount(message_journal_t *j, const hal_flash_t *flash, uint32_t base, uint32_t size)
{
	*j = (message_journal_t){0};
	if (!flash || !flash->read || !flash->write || !flash->erase || flash->erase_size != SECTOR ||
	    base % SECTOR || size % SECTOR || size < 2u * SECTOR || base > flash->size || size > flash->size - base) {
		return false;
	}
	message_journal_t scan = { flash, base, size / SECTOR, 0 };
	for (uint32_t offset = 0; offset < size; offset += SECTOR) {
		uint32_t generation;
		if (!sector_header(&scan, base + offset, &generation)) {
			return false;
		}
		if (generation > scan.generation) {
			scan.generation = generation;
		}
	}
	*j = scan;
	return true;
}

static bool erased(const uint8_t *data, uint32_t len)
{
	while (len-- > 0) {
		if (*data++ != 0xFFu) {
			return false;
		}
	}
	return true;
}

static bool sector_erased(const hal_flash_t *flash, uint32_t addr, uint8_t *scratch, bool *blank)
{
	*blank = false;
	for (uint32_t offset = 0; offset < SECTOR; offset += SLOT) {
		uint32_t len = SECTOR - offset < SLOT ? SECTOR - offset : SLOT;
		if (!flash->read(addr + offset, scratch, len)) {
			return false;
		}
		if (!erased(scratch, len)) {
			return true;
		}
	}
	*blank = true;
	return true;
}

bool message_journal_append(message_journal_t *j, const message_journal_record_t *record, bool reclaim,
			    uint8_t scratch[SLOT], uint32_t *id)
{
	if (!j->flash || !memchr(record->text, '\0', sizeof(record->text))) {
		return false;
	}
	const hal_flash_t *flash = j->flash;
	uint32_t g = j->generation, slot = SLOTS, addr = 0;
	if (g != 0) {
		addr = sector_addr(j, g);
		// Continue after the highest programmed slot even if a power cut left a hole.
		while (slot != 0) {
			if (!flash->read(addr + HEADER_SIZE + (slot - 1u) * SLOT, scratch, SLOT)) {
				return false;
			}
			if (!erased(scratch, SLOT)) {
				break;
			}
			slot--;
		}
	}
	bool rollover = slot == SLOTS;
	if (rollover) {
		if (g == MAX_GENERATION) {
			return false;
		}
		addr = sector_addr(j, ++g);
		uint32_t prior;
		bool blank;
		if (!sector_header(j, addr, &prior) || !sector_erased(flash, addr, scratch, &blank)) {
			return false;
		}
		bool expired = prior != 0 && prior < g && g - prior >= j->sectors;
		if (!blank && (!(reclaim || expired) || !flash->erase(addr))) {
			return false;
		}
		slot = 0;
	}
	uint32_t key = (g - 1u) * SLOTS + slot + 1u;
	uint32_t at = addr + HEADER_SIZE + slot * SLOT;
	memset(scratch, 0, RECORD_CRC);
	put32(scratch, key);
	put32(scratch + 4, record->src);
	put32(scratch + 8, record->dst);
	put32(scratch + 12, record->time);
	scratch[16] = record->flags;
	memcpy(scratch + TEXT_OFFSET, record->text, strlen(record->text) + 1u);
	put32(scratch + RECORD_CRC, storage_crc32(0, scratch, RECORD_CRC));
	uint8_t commit = 0;
	if (!flash->write(at, scratch, RECORD_CRC + 4u) || !flash->write(at + SLOT - 1u, &commit, 1)) {
		return false;
	}
	if (rollover) {
		// The new sector becomes visible only after its first record is committed.
		put32(scratch, MAGIC);
		put32(scratch + 4, g);
		put32(scratch + HEADER_CRC, storage_crc32(0, scratch, HEADER_CRC));
		if (!flash->write(addr, scratch, HEADER_CRC + 4u) || !flash->write(addr + HEADER_SIZE - 1u, &commit, 1)) {
			return false;
		}
		j->generation = g;
	}
	*id = key;
	return true;
}

bool message_journal_read(const message_journal_t *j, uint32_t id, uint8_t scratch[SLOT], message_journal_record_t *record)
{
	if (!j->flash || id == 0) {
		return false;
	}
	uint32_t g = (id - 1u) / SLOTS + 1u;
	// Leave one sector outside the live window so a rollover can fail without losing a live record.
	if (g > j->generation || j->generation - g >= j->sectors - 1u) {
		return false;
	}
	uint32_t addr = sector_addr(j, g), actual;
	uint32_t at = addr + HEADER_SIZE + ((id - 1u) % SLOTS) * SLOT;
	if (!sector_header(j, addr, &actual) || actual != g || !j->flash->read(at, scratch, SLOT) ||
	    scratch[SLOT - 1u] != 0 || get32(scratch) != id || !memchr(scratch + TEXT_OFFSET, '\0', MESSAGE_JOURNAL_TEXT) ||
	    get32(scratch + RECORD_CRC) != storage_crc32(0, scratch, RECORD_CRC)) {
		return false;
	}
	record->src = get32(scratch + 4);
	record->dst = get32(scratch + 8);
	record->time = get32(scratch + 12);
	record->flags = scratch[16];
	memcpy(record->text, scratch + TEXT_OFFSET, MESSAGE_JOURNAL_TEXT);
	return true;
}
#endif

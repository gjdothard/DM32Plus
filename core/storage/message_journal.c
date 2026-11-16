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

static bool sector_header(const message_journal_t *j, uint32_t addr, uint32_t *generation, bool prepared)
{
	uint8_t h[HEADER_SIZE];
	if (!j->flash->read(addr, h, sizeof(h))) {
		return false;
	}
	uint32_t g = get32(h + 4);
	*generation = 0;
	if (g != 0 && g <= MAX_GENERATION && sector_addr(j, g) == addr && get32(h) == MAGIC &&
	    (h[HEADER_SIZE - 1u] != 0) == prepared &&
	    get32(h + HEADER_CRC) == storage_crc32(0, h, HEADER_CRC)) {
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
		if (!sector_header(&scan, base + offset, &generation, false)) {
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

static void record_encode(uint8_t *dst, const message_journal_record_t *record, uint32_t key)
{
	memset(dst, 0, RECORD_CRC);
	put32(dst, key);
	put32(dst + 4, record->src);
	put32(dst + 8, record->dst);
	put32(dst + 12, record->time);
	dst[16] = record->flags;
	memcpy(dst + TEXT_OFFSET, record->text, strlen(record->text) + 1u);
	put32(dst + RECORD_CRC, storage_crc32(0, dst, RECORD_CRC));
}

static bool record_valid(const uint8_t *data, uint32_t key)
{
	return data[SLOT - 1u] == 0 && get32(data) == key &&
	       memchr(data + TEXT_OFFSET, '\0', MESSAGE_JOURNAL_TEXT) &&
	       get32(data + RECORD_CRC) == storage_crc32(0, data, RECORD_CRC);
}

static bool rollover(message_journal_t *j, uint32_t g, const message_journal_record_t *record,
		     bool (*replace)(uint32_t, const uint8_t *), uint8_t *scratch, bool *recovered)
{
	const hal_flash_t *flash = j->flash;
	uint32_t addr = sector_addr(j, g);
	uint32_t key = (g - 1u) * SLOTS + 1u;
	uint32_t pending;
	if (!sector_header(j, addr, &pending, true)) {
		return false;
	}
	*recovered = pending == g;
	if (*recovered) {
		if (!flash->read(addr + HEADER_SIZE, scratch, SLOT) || !record_valid(scratch, key)) {
			return false;
		}
	} else {
		uint32_t prior;
		bool blank;
		if (!sector_header(j, addr, &prior, false) || !sector_erased(flash, addr, scratch, &blank)) {
			return false;
		}
		bool expired = prior != 0 && prior < g && g - prior >= j->sectors;
		if (!blank && !expired) {
			return false;
		}
		memset(scratch, 0xFF, SECTOR);
		put32(scratch, MAGIC);
		put32(scratch + 4, g);
		put32(scratch + HEADER_CRC, storage_crc32(0, scratch, HEADER_CRC));
		record_encode(scratch + HEADER_SIZE, record, key);
		scratch[HEADER_SIZE + SLOT - 1u] = 0;
		if (!replace(addr, scratch)) {
			return false;
		}
	}
	// The old live window survives until the replacement's complete first record can be read.
	uint8_t commit = 0;
	if (!flash->write(addr + HEADER_SIZE - 1u, &commit, 1)) {
		return false;
	}
	j->generation = g;
	return true;
}

bool message_journal_append(message_journal_t *j, const message_journal_record_t *record,
			    bool (*replace)(uint32_t, const uint8_t *), uint8_t *scratch, uint32_t *id)
{
	if (!j->flash || !replace || !memchr(record->text, '\0', sizeof(record->text))) {
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
	if (slot == SLOTS) {
		if (g == MAX_GENERATION) {
			return false;
		}
		bool recovered;
		if (!rollover(j, ++g, record, replace, scratch, &recovered)) {
			return false;
		}
		if (!recovered) {
			*id = (g - 1u) * SLOTS + 1u;
			return true;
		}
		addr = sector_addr(j, g);
		slot = 1;
	}
	uint32_t key = (g - 1u) * SLOTS + slot + 1u;
	uint32_t at = addr + HEADER_SIZE + slot * SLOT;
	record_encode(scratch, record, key);
	uint8_t commit = 0;
	if (!flash->write(at, scratch, RECORD_CRC + 4u) || !flash->write(at + SLOT - 1u, &commit, 1)) {
		return false;
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
	if (!sector_header(j, addr, &actual, false) || actual != g || !j->flash->read(at, scratch, SLOT) ||
	    !record_valid(scratch, id)) {
		return false;
	}
	record->src = get32(scratch + 4);
	record->dst = get32(scratch + 8);
	record->time = get32(scratch + 12);
	record->flags = scratch[16];
	memcpy(record->text, scratch + TEXT_OFFSET, MESSAGE_JOURNAL_TEXT);
	return true;
}

static bool overlaps(uint32_t base, uint32_t size, uint32_t addr, uint32_t length)
{
	return length != 0 && (addr >= base ? addr - base < size : base - addr < length);
}

static bool reserve_valid(const hal_flash_map_t *m)
{
	uint32_t base = m->message_addr, size = m->message_size;
	return size != 0 && m->openfw_end >= m->openfw_flash &&
	       !overlaps(base, size, m->openfw_flash, m->openfw_end - m->openfw_flash) &&
	       !overlaps(base, size, m->openfw_eeprom, 0x10000u) &&
	       !overlaps(base, size, m->settings_addr, m->settings_size) &&
	       !overlaps(base, size, m->journal_addr, 2u * SECTOR) &&
	       !overlaps(base, size, m->calibration_addr, m->calibration_size) &&
	       !overlaps(base, size, m->native_addr, m->native_size) &&
	       !overlaps(base, size, m->staging_addr, m->staging_size);
}

// This marker occupies its own sector, outside the record ring. A new marker is allowed
// only on an erased reserve, or on a retry of a torn marker with the reserve still blank.
static const uint8_t activation[] = { 'R', 'F', 'M', 'S', 'G', '1', 0x3C, 0xA5, 0x00 };

static message_journal_result_t activate_reserve(const hal_flash_t *flash, const hal_flash_map_t *m, uint8_t *scratch)
{
	uint32_t i = 0;
	while (i < sizeof(activation) && scratch[i] == activation[i]) {
		i++;
	}
	// A NOR interruption can program some bits in the next byte. Later bytes must be erased.
	if (i < sizeof(activation) && (scratch[i] & activation[i]) != activation[i]) {
		return MESSAGE_JOB_FOREIGN;
	}
	uint32_t first = i < sizeof(activation) ? i + 1u : i;
	if (!erased(scratch + first, SECTOR - first)) {
		return MESSAGE_JOB_FOREIGN;
	}
	for (uint32_t offset = SECTOR; offset < m->message_size; offset += SECTOR) {
		if (!flash->read(m->message_addr + offset, scratch, SECTOR)) {
			return MESSAGE_JOB_FAILED;
		}
		if (!erased(scratch, SECTOR)) {
			return MESSAGE_JOB_FOREIGN;
		}
	}
	// Program a byte at a time so a cut always leaves a recognizable prefix.
	for (; i < sizeof(activation); i++) {
		if (!flash->write(m->message_addr + i, activation + i, 1)) {
			return MESSAGE_JOB_FAILED;
		}
	}
	return MESSAGE_JOB_OK;
}

static message_journal_result_t run_job(const hal_flash_map_t *m, const message_journal_job_t *job,
					       bool activate, uint8_t *scratch, uint32_t *id)
{
	message_journal_t journal;
	const hal_flash_t *flash = board.flash;
	if (!storage_ready() || m->message_size < 3u * SECTOR ||
	    m->message_addr > flash->size || m->message_size > flash->size - m->message_addr ||
	    !message_journal_mount(&journal, flash, m->message_addr + SECTOR, m->message_size - SECTOR)) {
		return MESSAGE_JOB_UNAVAILABLE;
	}
	if (!flash->read(m->message_addr, scratch, activate ? SECTOR : sizeof(activation))) {
		return MESSAGE_JOB_FAILED;
	}
	bool active = memcmp(scratch, activation, sizeof(activation)) == 0;
	if (activate) {
		return active ? MESSAGE_JOB_OK : activate_reserve(flash, m, scratch);
	}
	if (!active) {
		return MESSAGE_JOB_INACTIVE;
	}
	return message_journal_append(&journal, job->record, storage_sector_write, scratch, id)
		? MESSAGE_JOB_OK : MESSAGE_JOB_FAILED;
}

static void journal_job(void *ctx, bool activate)
{
	const message_journal_job_t *job = ctx;
	const hal_flash_map_t *m = storage_map();
	if (!m || !reserve_valid(m)) {
		job->complete(MESSAGE_JOB_UNAVAILABLE, 0);
		return;
	}
	uint8_t *scratch = storage_buffer_acquire();
	if (!scratch) {
		job->complete(MESSAGE_JOB_BUSY, 0);
		return;
	}
	uint32_t id = 0;
	storage_lock();
	message_journal_result_t result = run_job(m, job, activate, scratch, &id);
	storage_unlock();
	storage_buffer_release();
	job->complete(result, id);
}

static void activation_job(void *ctx) { journal_job(ctx, true); }
static void append_job(void *ctx) { journal_job(ctx, false); }

bool message_journal_activate(message_journal_job_t *job)
{
	return job && job->complete && storage_job_post(activation_job, job);
}

bool message_journal_submit(message_journal_job_t *job)
{
	return job && job->complete && job->record && storage_job_post(append_job, job);
}
#endif

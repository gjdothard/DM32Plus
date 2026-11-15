// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage/message_journal.h"
#include "storage/storage.h"

#define SECTORS 3u
#define BYTES (SECTORS * STORAGE_SECTOR)
#define SLOTS 21u
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static uint8_t data[BYTES], snapshot[BYTES], scratch[MESSAGE_JOURNAL_SCRATCH];
static unsigned budget;
const board_t board = {0};
void storage_lock(void) {}
void storage_unlock(void) {}

static bool read_flash(uint32_t addr, void *out, uint32_t len)
{
	CHECK(addr <= BYTES && len <= BYTES - addr);
	memcpy(out, data + addr, len);
	return true;
}

static bool write_flash(uint32_t addr, const void *buf, uint32_t len)
{
	const uint8_t *p = buf;
	CHECK(addr <= BYTES && len <= BYTES - addr);
	while (len-- > 0) {
		if (budget == 0) {
			return false;
		}
		budget--;
		CHECK((data[addr] & *p) == *p);
		data[addr++] = *p++;
	}
	return true;
}

static bool erase_flash(uint32_t addr)
{
	CHECK(addr < BYTES && addr % STORAGE_SECTOR == 0);
	unsigned bytes = budget < STORAGE_SECTOR ? budget : STORAGE_SECTOR;
	memset(data + addr, 0xFF, bytes);
	budget -= bytes;
	return bytes == STORAGE_SECTOR;
}

static const hal_flash_t flash = {
	.size = BYTES, .erase_size = STORAGE_SECTOR,
	.read = read_flash, .write = write_flash, .erase = erase_flash,
};
static const message_journal_record_t entry = {
	.src = 1002003u, .dst = 1234567u, .time = 1770000000u, .flags = 5, .text = "Test message",
};

static void mount(message_journal_t *j)
{
	CHECK(message_journal_mount(j, &flash, 0, BYTES));
}

static void append(message_journal_t *j, bool reclaim, uint32_t wanted)
{
	uint32_t id = 0;
	budget = BYTES;
	CHECK(message_journal_append(j, &entry, reclaim, scratch, &id) && id == wanted);
}

static bool readable(const message_journal_t *j, uint32_t id)
{
	message_journal_record_t out;
	if (!message_journal_read(j, id, scratch, &out)) {
		return false;
	}
	CHECK(out.src == entry.src && out.dst == entry.dst && out.time == entry.time &&
	      out.flags == entry.flags && strcmp(out.text, entry.text) == 0);
	return true;
}

static void cut_rollover(uint32_t next, uint32_t first_live, unsigned erase_bytes)
{
	// Try every program byte boundary and representative points inside the erase.
	memcpy(snapshot, data, BYTES);
	for (unsigned cut = 0; cut < erase_bytes + 179u; cut += cut < erase_bytes ? 64u : 1u) {
		message_journal_t j;
		memcpy(data, snapshot, BYTES);
		mount(&j);
		budget = cut;
		uint32_t id;
		CHECK(!message_journal_append(&j, &entry, true, scratch, &id));
		mount(&j);
		CHECK(readable(&j, first_live) && readable(&j, next - 1u) && !readable(&j, next));
		append(&j, true, next);
		mount(&j);
		CHECK(readable(&j, next) && !readable(&j, first_live));
	}
	memcpy(data, snapshot, BYTES);
}

int main(void)
{
	message_journal_t j;
	memset(data, 0xFF, BYTES);
	CHECK(!message_journal_mount(&j, &flash, 1, BYTES - 1u));
	CHECK(!message_journal_mount(&j, &flash, 0, STORAGE_SECTOR));
	data[STORAGE_SECTOR - 1u] = 0x42;
	mount(&j);
	CHECK(!readable(&j, 1));
	uint32_t id;
	budget = BYTES;
	CHECK(!message_journal_append(&j, &entry, false, scratch, &id));
	CHECK(data[STORAGE_SECTOR - 1u] == 0x42 && budget == BYTES);
	append(&j, true, 1);
	budget = 32;
	CHECK(!message_journal_append(&j, &entry, false, scratch, &id));
	mount(&j);
	CHECK(readable(&j, 1) && !readable(&j, 2));
	for (unsigned n = 3; n <= 2u * SLOTS; n++) {
		append(&j, false, n);
	}
	cut_rollover(2u * SLOTS + 1u, 1, 0);
	mount(&j);
	for (unsigned n = 2u * SLOTS + 1u; n <= 3u * SLOTS; n++) {
		append(&j, false, n);
	}
	cut_rollover(3u * SLOTS + 1u, SLOTS + 1u, STORAGE_SECTOR);
	mount(&j);
	append(&j, false, 3u * SLOTS + 1u);
	mount(&j);
	CHECK(!readable(&j, SLOTS + 1u) && readable(&j, 3u * SLOTS + 1u));
	data[16u + 17u] ^= 0x01;
	CHECK(!readable(&j, 3u * SLOTS + 1u));

	message_journal_record_t longest = entry, out;
	memset(longest.text, 'X', sizeof(longest.text));
	budget = BYTES;
	CHECK(!message_journal_append(&j, &longest, false, scratch, &id));
	longest.text[sizeof(longest.text) - 1u] = '\0';
	CHECK(message_journal_append(&j, &longest, false, scratch, &id));
	mount(&j);
	CHECK(message_journal_read(&j, id, scratch, &out));
	CHECK(memcmp(out.text, longest.text, sizeof(out.text)) == 0);
	puts("# messageJournalTest PASSED");
	return 0;
}

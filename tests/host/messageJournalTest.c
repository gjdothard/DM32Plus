// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage/message_journal.h"
#include "storage/storage.h"

#define SECTORS 3u
#define BYTES (SECTORS * STORAGE_SECTOR)
#define FLASH_BYTES 0x20000u
#define RESERVE_BASE 0x10000u
#define SLOTS 21u
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static uint8_t data[FLASH_BYTES], snapshot[FLASH_BYTES], scratch[STORAGE_SECTOR];
static unsigned budget;
static uint32_t failed_read = UINT32_MAX;
static uint32_t corrupt_write = UINT32_MAX;
void storage_lock(void) {}
void storage_unlock(void) {}
void storage_quick_lock(void) {}
void storage_quick_unlock(void) {}

static bool read_flash(uint32_t addr, void *out, uint32_t len)
{
	CHECK(addr <= FLASH_BYTES && len <= FLASH_BYTES - addr);
	if (addr == failed_read)
		return false;
	memcpy(out, data + addr, len);
	return true;
}

static bool write_flash(uint32_t addr, const void *buf, uint32_t len)
{
	const uint8_t *p = buf;
	CHECK(addr <= FLASH_BYTES && len <= FLASH_BYTES - addr);
	while (len-- > 0) {
		if (budget == 0) {
			return false;
		}
		budget--;
		CHECK((data[addr] & *p) == *p);
		data[addr++] = *p++;
		if (addr - 1u == corrupt_write) {
			data[addr - 1u] &= 0xFBu;
			corrupt_write = UINT32_MAX;
		}
	}
	return true;
}

static bool erase_flash(uint32_t addr)
{
	CHECK(addr < FLASH_BYTES && addr % STORAGE_SECTOR == 0);
	unsigned bytes = budget < STORAGE_SECTOR ? budget : STORAGE_SECTOR;
	memset(data + addr, 0xFF, bytes);
	budget -= bytes;
	return bytes == STORAGE_SECTOR;
}

static const hal_flash_t flash = {
	.size = BYTES, .erase_size = STORAGE_SECTOR,
	.read = read_flash, .write = write_flash, .erase = erase_flash,
};
static hal_flash_map_t job_map = {
	.settings_addr = 0x14000u, .settings_size = 2u * STORAGE_SECTOR, .journal_addr = 0x16000u,
	.message_addr = RESERVE_BASE, .message_size = BYTES,
};
static const hal_flash_t job_flash = {
	.size = FLASH_BYTES, .erase_size = STORAGE_SECTOR, .map = &job_map,
	.read = read_flash, .write = write_flash, .erase = erase_flash,
};
const board_t board = { .flash = &job_flash };
static const message_journal_record_t entry = {
	.src = 1002003u, .dst = 1234567u, .time = 1770000000u, .flags = 5, .text = "Test message",
};

static void mount(message_journal_t *j)
{
	CHECK(message_journal_mount(j, &flash, 0, BYTES));
}

static void append(message_journal_t *j, uint32_t wanted)
{
	uint32_t id = 0;
	budget = FLASH_BYTES;
	CHECK(message_journal_append(j, &entry, storage_sector_write, scratch, &id) && id == wanted);
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

static void cut_rollover(uint32_t next, uint32_t first_live)
{
	// Cut while staging the replacement, writing J0, erasing and writing the target,
	// and between the storage and message journal commit marks.
	const unsigned commit_at = 3u * STORAGE_SECTOR + 16u;
	const unsigned cuts[] = {
		0, 17, STORAGE_SECTOR, STORAGE_SECTOR + 53u, 2u * STORAGE_SECTOR + 256u,
		commit_at - 1u, commit_at, commit_at + 79u, commit_at + STORAGE_SECTOR - 1u,
		commit_at + STORAGE_SECTOR + 177u, commit_at + 2u * STORAGE_SECTOR,
		commit_at + 2u * STORAGE_SECTOR + 1u,
	};
	memcpy(snapshot, data, sizeof(data));
	for (unsigned i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
		message_journal_t j;
		memcpy(data, snapshot, sizeof(data));
		mount(&j);
		budget = cuts[i];
		uint32_t id;
		CHECK(!message_journal_append(&j, &entry, storage_sector_write, scratch, &id));
		budget = FLASH_BYTES;
		CHECK(storage_init());
		mount(&j);
		uint32_t addr = (((next - 1u) / SLOTS) % SECTORS) * STORAGE_SECTOR;
		if (cuts[i] == commit_at) {
			unsigned before = budget;
			data[addr + 16u + 17u] ^= 1u;
			CHECK(!message_journal_append(&j, &entry, storage_sector_write, scratch, &id));
			CHECK(data[addr + 15u] == 0xFFu && budget == before);
			data[addr + 16u + 17u] ^= 1u;
		}
		if (cuts[i] == commit_at + 2u * STORAGE_SECTOR + 1u) {
			data[addr + 15u] = 0x80;	// power cut during the last byte's program pulse
		}
		CHECK(readable(&j, first_live) && readable(&j, next - 1u) && !readable(&j, next));
		bool staged = cuts[i] >= commit_at;
		append(&j, next + staged);
		mount(&j);
		CHECK(readable(&j, next) && !readable(&j, first_live));
		if (staged) {
			CHECK(readable(&j, next + 1u));
		}
	}
	memcpy(data, snapshot, sizeof(data));
}

static void cut_replay(uint32_t next, uint32_t first_live)
{
	const unsigned cuts[] = {
		0, 17, STORAGE_SECTOR, STORAGE_SECTOR + 177u, 2u * STORAGE_SECTOR, FLASH_BYTES, FLASH_BYTES + 1u,
	};
	const uint32_t done = job_map.journal_addr + 16u;
	const uint32_t addr = (((next - 1u) / SLOTS) % SECTORS) * STORAGE_SECTOR;
	memcpy(snapshot, data, sizeof(data));
	for (unsigned i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
		memcpy(data, snapshot, sizeof(data));
		message_journal_t j;
		mount(&j);
		budget = 3u * STORAGE_SECTOR + 16u; // J0 committed, target not erased
		uint32_t id;
		CHECK(!message_journal_append(&j, &entry, storage_sector_write, scratch, &id));
		CHECK(data[done] == 0xFFu);

		budget = cuts[i];
		if (cuts[i] == FLASH_BYTES)
			corrupt_write = addr + 16u + 17u; // silently corrupt the first text byte during replay
		if (cuts[i] == FLASH_BYTES + 1u)
			failed_read = job_map.journal_addr + STORAGE_SECTOR;
		CHECK(!storage_init() && !storage_ready());
		CHECK(data[done] == 0xFFu && corrupt_write == UINT32_MAX);
		failed_read = UINT32_MAX;
		mount(&j);
		CHECK(readable(&j, first_live) && readable(&j, next - 1u) && !readable(&j, next));
		budget = FLASH_BYTES;
		CHECK(!storage_sector_write(addr, scratch) && data[done] == 0xFFu);

		CHECK(storage_init());
		CHECK(data[done] == 0);
		mount(&j);
		append(&j, next + 1u);
		mount(&j);
		CHECK(readable(&j, next) && readable(&j, next + 1u) && !readable(&j, first_live));
	}
	memcpy(data, snapshot, sizeof(data));
}

static unsigned completions;
static message_journal_result_t completion;
static uint32_t committed_id;

static void complete(message_journal_result_t result, uint32_t id)
{
	completions++;
	completion = result;
	committed_id = id;
}

static void run_job(message_journal_job_t *job, bool activate, message_journal_result_t expected)
{
	unsigned before = completions;
	CHECK(activate ? message_journal_activate(job) : message_journal_submit(job));
	CHECK(!message_journal_activate(job) && !message_journal_submit(job) && completions == before);
	storage_job_poll();
	CHECK(completions == before + 1 && completion == expected && !storage_job_busy());
}

static void test_deferred_append(void)
{
	message_journal_job_t job = { .record = &entry, .complete = complete };
	memset(data, 0xFF, sizeof(data));
	budget = FLASH_BYTES;
	job_map.message_size = 0;
	run_job(&job, true, MESSAGE_JOB_UNAVAILABLE);
	job_map.message_size = BYTES;
	CHECK(storage_init());

	job_map.openfw_flash = 0x8000u;
	job_map.openfw_end = RESERVE_BASE + STORAGE_SECTOR;
	run_job(&job, true, MESSAGE_JOB_UNAVAILABLE);
	job_map.openfw_end = RESERVE_BASE;

	CHECK(storage_buffer_acquire());
	run_job(&job, true, MESSAGE_JOB_BUSY);
	storage_buffer_release();

	run_job(&job, false, MESSAGE_JOB_INACTIVE);
	data[RESERVE_BASE + BYTES - 1u] = 0x42;
	unsigned before = budget;
	run_job(&job, true, MESSAGE_JOB_FOREIGN);
	CHECK(data[RESERVE_BASE + BYTES - 1u] == 0x42 && budget == before && committed_id == 0);
	data[RESERVE_BASE + BYTES - 1u] = 0xFF;
	data[RESERVE_BASE] = 0x00;
	run_job(&job, true, MESSAGE_JOB_FOREIGN);
	CHECK(data[RESERVE_BASE] == 0x00 && budget == before);

	// Every torn-marker byte boundary leaves append disabled until activation is retried.
	for (unsigned cut = 0; cut < 9u; cut++) {
		memset(data + RESERVE_BASE, 0xFF, BYTES);
		budget = cut;
		run_job(&job, true, MESSAGE_JOB_FAILED);
		run_job(&job, false, MESSAGE_JOB_INACTIVE);
		budget = FLASH_BYTES;
		run_job(&job, true, MESSAGE_JOB_OK);
	}
	memset(data + RESERVE_BASE, 0xFF, BYTES);
	data[RESERVE_BASE] = (uint8_t)('R' | 0x80); // a cut with some intended zero bits still set
	run_job(&job, false, MESSAGE_JOB_INACTIVE);
	run_job(&job, true, MESSAGE_JOB_OK);
	CHECK(storage_init());
	run_job(&job, false, MESSAGE_JOB_OK);
	CHECK(committed_id == 1);
	before = budget;
	run_job(&job, true, MESSAGE_JOB_OK);
	CHECK(budget == before);

	message_journal_t j;
	CHECK(message_journal_mount(&j, &job_flash, RESERVE_BASE + STORAGE_SECTOR, BYTES - STORAGE_SECTOR));
	CHECK(readable(&j, 1));
	// An invalidated ownership marker stops append without erasing the archived record.
	data[RESERVE_BASE] &= 0xEF;
	run_job(&job, false, MESSAGE_JOB_INACTIVE);
	CHECK(readable(&j, 1));
}

static void test_capture(void)
{
	static message_history_t history;
	memset(data, 0xFF, sizeof(data));
	budget = FLASH_BYTES;
	CHECK(storage_init());
	message_history_start(&history, false);
	message_history_capture(1, 2, 0, 0, "Skipped while off");
	CHECK(history.count == 0);
	message_journal_job_t activation = { .complete = complete };
	run_job(&activation, true, MESSAGE_JOB_OK);
	unsigned before = budget;
	message_history_capture(3, 9, MESSAGE_RECORD_GROUP, 756, "Incoming");
	message_history_capture(9, 4, MESSAGE_RECORD_OUTGOING | MESSAGE_RECORD_FAILED, 0, "Outgoing");
	message_history_capture(3, 9, 0, 0, "Dropped");
	CHECK(budget == before && message_history_dropped() == 1);
	CHECK(storage_buffer_acquire());
	message_history_poll();
	CHECK(history.count == 2 && budget == before);
	storage_buffer_release();
	message_history_poll();
	message_history_poll();
	CHECK(history.count == 0 && message_history_dropped() == 1);
	CHECK(storage_init());
	message_history_start(&history, false);
	CHECK(history.active);
	message_journal_t j;
	message_journal_record_t out;
	CHECK(message_journal_mount(&j, &job_flash, RESERVE_BASE + STORAGE_SECTOR, BYTES - STORAGE_SECTOR));
	CHECK(message_journal_read(&j, 1, scratch, &out));
	CHECK(out.src == 3 && out.dst == 9 && out.flags == MESSAGE_RECORD_GROUP && out.time == 756 && !strcmp(out.text, "Incoming"));
	CHECK(message_journal_read(&j, 2, scratch, &out));
	CHECK(out.src == 9 && out.dst == 4 && out.flags == (MESSAGE_RECORD_OUTGOING | MESSAGE_RECORD_FAILED));
	CHECK(!strcmp(out.text, "Outgoing") && !message_journal_read(&j, 3, scratch, &out));
	message_history_capture(5, 9, 0, 0, "Marker lost");
	data[RESERVE_BASE] = 0;
	message_history_poll();
	CHECK(!history.active && !history.count && message_history_dropped() == 1);
	CHECK(history.view.status == HISTORY_ERROR);
	CHECK(!message_journal_read(&j, 3, scratch, &out));
}

static uint8_t page_count;
static uint32_t next_before;

static void page_complete(message_journal_result_t result, uint8_t count, uint32_t next)
{
	completion = result;
	page_count = count;
	next_before = next;
	completions++;
}

static void read_page(message_history_page_t *page, message_journal_result_t expected)
{
	unsigned before = completions, writes = budget;
	CHECK(message_history_read_page(page));
	CHECK(!message_history_read_page(page) && completions == before);
	storage_job_poll();
	CHECK(completions == before + 1u && completion == expected && budget == writes);
}

static void save_history(uint32_t src, uint32_t dst, uint8_t flags, const char *text)
{
	budget = FLASH_BYTES;
	message_history_capture(src, dst, flags, 0, text);
	message_history_poll();
}

static void test_default_conversations(void)
{
	static message_history_t state;
	memset(data, 0xFF, sizeof(data));
	budget = FLASH_BYTES;
	CHECK(storage_init());
	data[RESERVE_BASE] = 0x00;
	unsigned foreign_budget = budget;
	message_history_start(&state, true);
	CHECK(!message_history_active() && data[RESERVE_BASE] == 0x00 && budget == foreign_budget);
	data[RESERVE_BASE] = 0xFF;
	message_history_start(&state, true);
	CHECK(message_history_active());
	unsigned writes = budget;
	message_history_start(&state, true);
	CHECK(message_history_active() && budget == writes);

	message_history_browse(0, 0);
	message_history_poll();
	save_history(1, 9, MESSAGE_RECORD_OUTGOING, "I started");
	save_history(9, 1, 0, "Reply");
	save_history(1, 9, MESSAGE_RECORD_LOCAL_AUTHOR, "Echo");
	save_history(1, 9, MESSAGE_RECORD_GROUP | MESSAGE_RECORD_OUTGOING, "Group started");
	save_history(3, 9, MESSAGE_RECORD_GROUP, "Group replied");
	save_history(1, 12, MESSAGE_RECORD_OUTGOING, "Outgoing only");
	message_history_poll();
	message_history_view_t directory;
	char previews[MESSAGE_JOURNAL_TEXT];
	message_history_view(&directory, previews, sizeof previews);
	CHECK(directory.status == HISTORY_READY && directory.count == 3);
	CHECK(directory.peers[0] == 12 && directory.peers[1] == (MESSAGE_HISTORY_GROUP_KEY | 9u) &&
	      directory.peers[2] == 9);
	CHECK(!strcmp(previews + 2, "Outgoing only") &&
	      !strcmp(previews + MESSAGE_DIRECTORY_PREVIEW + 2, "Group replied") &&
	      !strcmp(previews + 2u * MESSAGE_DIRECTORY_PREVIEW + 2, "Echo"));
}

static void test_archive_pages(void)
{
	static message_history_t state;
	memset(data, 0xFF, sizeof(data));
	budget = FLASH_BYTES;
	job_map.message_size = 4u * STORAGE_SECTOR;
	CHECK(storage_init());
	message_history_start(&state, false);
	message_history_item_t items[2];
	message_history_page_t page = { .items = items, .complete = page_complete, .capacity = 2 };
	read_page(&page, MESSAGE_JOB_INACTIVE);
	CHECK(!page_count && !next_before);
	message_journal_job_t activation = { .complete = complete };
	run_job(&activation, true, MESSAGE_JOB_OK);
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(!page_count && !next_before);

	save_history(3, 1, 0, "Old private");
	save_history(1, 9, MESSAGE_RECORD_OUTGOING, "Private out");
	static const char private_text[] =
		"Private message with several wrapped lines. Bring the spare battery and meet at the north lot.";
	save_history(9, 1, 0, private_text);
	save_history(1, 9, MESSAGE_RECORD_GROUP | MESSAGE_RECORD_OUTGOING, "Group out");
	for (unsigned n = 5; n <= 23; n++)
		save_history(4, 9, MESSAGE_RECORD_GROUP, "Group in");

	read_page(&page, MESSAGE_JOB_OK);
	CHECK(page_count == 2 && items[0].id == 23 && items[1].id == 22 && next_before == 22);
	page.before = next_before;
	save_history(4, 9, MESSAGE_RECORD_GROUP, "New message");
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(page_count == 2 && items[0].id == 21 && items[1].id == 20);

	page.before = 0;
	page.peer = (message_history_peer_t){ .id = 9 };
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(page_count == 0 && next_before != 0);
	page.before = next_before;
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(page_count == 2 && items[0].id == 3 && items[1].id == 2);
	CHECK(!strcmp(items[0].record.text, private_text) && !strcmp(items[1].record.text, "Private out"));

	page.before = 0;
	page.peer.group = true;
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(page_count == 2 && items[0].id == 24 && items[1].id == 23);
	page.before = 5;
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(page_count == 1 && items[0].id == 4 && !next_before);

	unsigned writes = budget;
	char body[MESSAGE_JOURNAL_TEXT];
	message_history_view_t view;
	message_history_browse(0, 0);
	message_history_poll();
	message_history_view(&view, NULL, 0);
	CHECK(view.status == HISTORY_LOADING && view.count == 1 && view.peers[0] == (MESSAGE_HISTORY_GROUP_KEY | 9u));
	message_history_poll();
	message_history_view(&view, NULL, 0);
	CHECK(view.status == HISTORY_READY && view.count == 3 && view.peers[1] == 9 && view.peers[2] == 3);
	message_history_browse(9, 0);
	message_history_poll();
	message_history_view(&view, body, sizeof body);
	CHECK(view.status == HISTORY_LOADING && !body[0]);
	message_history_poll();
	message_history_view(&view, body, sizeof body);
	CHECK(view.status == HISTORY_READY && !strcmp(body, private_text) && view.before == 3);
	message_history_browse(9, view.before);
	message_history_poll();
	message_history_view(&view, body, sizeof body);
	CHECK(view.status == HISTORY_READY && !strcmp(body, "Private out") && (view.flags & MESSAGE_RECORD_OUTGOING));
	message_history_thread(9, 0, false);
	message_history_poll();
	message_history_poll();
	message_history_view(&view, body, sizeof body);
	CHECK(view.timeline && !view.forward && view.status == HISTORY_READY && view.count == 2);
	CHECK(view.thread[0].id == 3 && view.thread[1].id == 2);
	message_history_thread_text(view.revision, 0, body, sizeof body);
	CHECK(!strcmp(body, private_text));
	message_history_thread_text(view.revision, 1, body, sizeof body);
	CHECK(!strcmp(body, "Private out"));
	message_history_thread_text(view.revision - 1u, 0, body, sizeof body);
	CHECK(!body[0]);
	message_history_thread(9, view.thread[1].id, true);
	message_history_poll();
	message_history_view(&view, body, sizeof body);
	CHECK(view.forward && view.status == HISTORY_READY && view.count == 1 && view.thread[0].id == 3);
	message_history_thread(9, 3, true);
	message_history_poll();
	message_history_poll();
	message_history_poll();
	message_history_view(&view, body, sizeof body);
	CHECK(view.status == HISTORY_READY && view.thread[0].id == 3 && !(view.flags & MESSAGE_THREAD_NEWER));
	CHECK(budget == writes);

	page.peer = (message_history_peer_t){0};
	failed_read = RESERVE_BASE + STORAGE_SECTOR + 16u + 3u * MESSAGE_JOURNAL_SCRATCH;
	read_page(&page, MESSAGE_JOB_FAILED);
	CHECK(!page_count && !next_before);
	failed_read = UINT32_MAX;
	CHECK(storage_buffer_acquire());
	read_page(&page, MESSAGE_JOB_BUSY);
	storage_buffer_release();
	for (unsigned n = 25; n <= 43; n++)
		save_history(4, 9, MESSAGE_RECORD_GROUP, "Rollover");
	read_page(&page, MESSAGE_JOB_OK);
	CHECK(!page_count && !next_before);	// the cursor predates the live window
	page.capacity = 0;
	CHECK(!message_history_read_page(&page));
}

int main(void)
{
	message_journal_t j;
	memset(data, 0xFF, sizeof(data));
	budget = FLASH_BYTES;
	CHECK(storage_init());
	CHECK(!message_journal_mount(&j, &flash, 1, BYTES - 1u));
	CHECK(!message_journal_mount(&j, &flash, 0, STORAGE_SECTOR));
	data[STORAGE_SECTOR - 1u] = 0x42;
	mount(&j);
	CHECK(!readable(&j, 1));
	uint32_t id;
	budget = BYTES;
	CHECK(!message_journal_append(&j, &entry, storage_sector_write, scratch, &id));
	CHECK(data[STORAGE_SECTOR - 1u] == 0x42 && budget == BYTES);
	data[STORAGE_SECTOR - 1u] = 0xFF;
	append(&j, 1);
	budget = 32;
	CHECK(!message_journal_append(&j, &entry, storage_sector_write, scratch, &id));
	mount(&j);
	CHECK(readable(&j, 1) && !readable(&j, 2));
	for (unsigned n = 3; n <= 2u * SLOTS; n++) {
		append(&j, n);
	}
	cut_rollover(2u * SLOTS + 1u, 1);
	mount(&j);
	for (unsigned n = 2u * SLOTS + 1u; n <= 3u * SLOTS; n++) {
		append(&j, n);
	}
	cut_rollover(3u * SLOTS + 1u, SLOTS + 1u);
	cut_replay(3u * SLOTS + 1u, SLOTS + 1u);
	mount(&j);
	append(&j, 3u * SLOTS + 1u);
	mount(&j);
	CHECK(!readable(&j, SLOTS + 1u) && readable(&j, 3u * SLOTS + 1u));
	data[16u + 17u] ^= 0x01;
	CHECK(!readable(&j, 3u * SLOTS + 1u));

	message_journal_record_t longest = entry, out;
	memset(longest.text, 'X', sizeof(longest.text));
	budget = BYTES;
	CHECK(!message_journal_append(&j, &longest, storage_sector_write, scratch, &id));
	longest.text[sizeof(longest.text) - 1u] = '\0';
	CHECK(message_journal_append(&j, &longest, storage_sector_write, scratch, &id));
	mount(&j);
	CHECK(message_journal_read(&j, id, scratch, &out));
	CHECK(memcmp(out.text, longest.text, sizeof(out.text)) == 0);
	test_deferred_append();
	test_capture();
	test_archive_pages();
	test_default_conversations();
	puts("# messageJournalTest PASSED");
	return 0;
}

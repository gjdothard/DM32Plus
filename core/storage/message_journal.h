// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef RFOUNDRY_MESSAGE_JOURNAL_H
#define RFOUNDRY_MESSAGE_JOURNAL_H

#include "config.h"
#include "hal_flash.h"
#include <stddef.h>

#define MESSAGE_JOURNAL_TEXT 144u
#define MESSAGE_JOURNAL_SCRATCH 192u
#define MESSAGE_HISTORY_QUEUE 2u
#define MESSAGE_HISTORY_PAGE_MAX 4u
#define MESSAGE_HISTORY_PEERS 6u
#define MESSAGE_DIRECTORY_PREVIEW (MESSAGE_JOURNAL_TEXT / MESSAGE_HISTORY_PEERS)
#define MESSAGE_THREAD_ROWS 3u
#define MESSAGE_THREAD_PREVIEW (MESSAGE_JOURNAL_TEXT / MESSAGE_THREAD_ROWS)
#define MESSAGE_THREAD_OLDER 0x01u
#define MESSAGE_THREAD_NEWER 0x02u
#define MESSAGE_HISTORY_GROUP_KEY 0x80000000u
#define MESSAGE_RECORD_GROUP 0x01u
#define MESSAGE_RECORD_OUTGOING 0x02u
#define MESSAGE_RECORD_FAILED 0x04u
#define MESSAGE_RECORD_DELIVERED 0x08u
#define MESSAGE_RECORD_LOCAL_AUTHOR 0x10u

typedef struct {
	uint32_t src, dst, time;
	uint8_t flags;	// MESSAGE_RECORD_*
	char text[MESSAGE_JOURNAL_TEXT];
} message_journal_record_t;

typedef struct {
	const hal_flash_t *flash;
	uint32_t base, sectors;
	uint32_t generation;
} message_journal_t;

// The caller owns and serializes access to the selected flash range and scratch buffer.
// Mount scans headers only and leaves unknown contents alone.
bool message_journal_mount(message_journal_t *j, const hal_flash_t *flash, uint32_t base, uint32_t size);
// Append needs one erase unit of scratch and a replacement function with reboot recovery.
// Remount after a failed write. Only erased or expired sectors may start a rollover.
bool message_journal_append(message_journal_t *j, const message_journal_record_t *record,
			    bool (*replace)(uint32_t, const uint8_t *), uint8_t *scratch, uint32_t *id);
bool message_journal_read(const message_journal_t *j, uint32_t id, uint8_t scratch[MESSAGE_JOURNAL_SCRATCH],
			  message_journal_record_t *record);

#if CONFIG_MESSAGE_JOURNAL
typedef enum {
	MESSAGE_JOB_OK,
	MESSAGE_JOB_BUSY,
	MESSAGE_JOB_UNAVAILABLE,
	MESSAGE_JOB_INACTIVE,
	MESSAGE_JOB_FOREIGN,
	MESSAGE_JOB_FAILED,
} message_journal_result_t;

typedef struct {
	const message_journal_record_t *record; // used by append only
	void (*complete)(message_journal_result_t result, uint32_t id);
} message_journal_job_t;

// Submit only after the owner approves activation. The activation job never erases flash.
bool message_journal_available(void);
bool message_journal_activate(const message_journal_job_t *job);
// Submit from the single storage-job producer. Keep the request and record unchanged and alive
// until the callback runs on the housekeeping task.
bool message_journal_submit(message_journal_job_t *job);

typedef struct {
	uint32_t id;
	bool group;
} message_history_peer_t;	// 0 matches every conversation

typedef struct {
	uint32_t id;		// stable journal ID
	message_journal_record_t record;
} message_history_item_t;

typedef struct {
	message_history_item_t *items;
	void (*complete)(message_journal_result_t result, uint8_t count, uint32_t next_before);
	message_history_peer_t peer;
	uint32_t before;	// exclusive record ID, 0 starts at the newest record
	uint8_t capacity;
} message_history_page_t;

// Submit from the UI's storage-job producer. Keep the request and output buffer alive until
// completion. Continue with next_before, even on an empty page. Zero next_before means the end.
bool message_history_read_page(message_history_page_t *page);

typedef enum { HISTORY_LOADING, HISTORY_READY, HISTORY_EMPTY, HISTORY_ERROR } message_history_status_t;

typedef struct {
	uint32_t id;
	uint16_t time;
	uint8_t flags;	// MESSAGE_RECORD_*
} message_thread_entry_t;

typedef struct {
	uint32_t key;	// 0 is the directory, bit 31 distinguishes a talkgroup
	union {
		uint32_t peers[MESSAGE_HISTORY_PEERS]; // directory
		uint32_t time; // selected message, UTC minute plus one or zero
		message_thread_entry_t thread[MESSAGE_THREAD_ROWS];
	};
	uint32_t before;	// single record: next older page; timeline: requested anchor
	uint16_t revision;
	uint8_t count, flags;
	uint8_t status;	// message_history_status_t
	bool timeline, forward;
} message_history_view_t;

// One UI browser, served on the housekeeping task. Text is copied only for a ready message.
bool message_history_active(void);
void message_history_browse(uint32_t key, uint32_t before);
// Load a page before or after an exclusive record ID. Zero starts at the newest.
void message_history_thread(uint32_t key, uint32_t anchor, bool forward);
void message_history_view(message_history_view_t *out, char *text, size_t size);

typedef struct {
	message_journal_record_t pending[MESSAGE_HISTORY_QUEUE];
	message_history_view_t view;
	char view_text[MESSAGE_JOURNAL_TEXT];
	uint32_t scan_before;
	uint32_t dropped;
	uint8_t head, count;
	bool active;
} message_history_t;

// The housekeeping task owns this queue for its lifetime and does all flash I/O.
void message_history_start(message_history_t *history, bool enable_by_default);
void message_history_poll(void);
// The radio task copies accepted SMS and terminal send results here without waiting on flash.
void message_history_capture(uint32_t src, uint32_t dst, uint8_t flags, uint16_t utc_minute, const char *text);
uint32_t message_history_dropped(void);
#endif

#endif

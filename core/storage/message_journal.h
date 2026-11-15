// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef RFOUNDRY_MESSAGE_JOURNAL_H
#define RFOUNDRY_MESSAGE_JOURNAL_H

#include "config.h"
#include "hal_flash.h"

#define MESSAGE_JOURNAL_TEXT 144u
#define MESSAGE_JOURNAL_SCRATCH 192u

typedef struct {
	uint32_t src, dst, time;
	uint8_t flags;	// reserved for the message service
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
// A previously used sector can be reclaimed only with migration permission. Remount after a failed write.
bool message_journal_append(message_journal_t *j, const message_journal_record_t *record, bool reclaim,
			    uint8_t scratch[MESSAGE_JOURNAL_SCRATCH], uint32_t *id);
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
bool message_journal_activate(message_journal_job_t *job);
// Submit from the single storage-job producer. Keep the request and record unchanged and alive
// until the callback runs on the housekeeping task.
bool message_journal_submit(message_journal_job_t *job);
#endif

#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include "config.h"
#if CONFIG_P25_UI && CONFIG_P25_TRUNK
#include <string.h>
#include "p25_favorites.h"
#include "codeplug/codeplug_kv.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_records.h"

#define FAVORITE_BYTES (9 + 2 * P25_MONITOR_FAVORITES)
static bool identity(const channel_t *ch, uint8_t out[7])
{
	codeplug_system_t sys;
	uint8_t payload[CODEPLUG_SYS_HDR + 10];
	if (!ch || ch->kind != CODEPLUG_KIND_P25_TRUNK || ch->system >= CODEPLUG_SYSTEMS_RUN ||
	    codeplug_system(ch->system, &sys, payload, 10) < 5 || sys.type != CODEPLUG_SYSTYPE_P25_TRUNK || sys.id_len != 5)
		return false;
	uint16_t check = codeplug_name_check(sys.name);
	out[0] = (uint8_t)check; out[1] = (uint8_t)(check >> 8);
	memcpy(out + 2, payload, 5);
	return true;
}

bool p25_favorites_read(const channel_t *ch, uint16_t out[P25_MONITOR_FAVORITES], uint8_t *count)
{
	uint8_t id[7], record[FAVORITE_BYTES];
	*count = 0;
	memset(out, 0, P25_MONITOR_FAVORITES * sizeof *out);
	if (!identity(ch, id)) return false;
	int len;
	if (!codeplug_kv_lookup((uint16_t)(CODEPLUG_KEY_P25_FAVORITES + ch->system), record, sizeof record, &len)) return false;
	if (len < 0) return true;
	// Replaced systems and malformed/future records never become an implicit listen list.
	if (len != sizeof record || record[0] != 1 || record[1] > P25_MONITOR_FAVORITES || memcmp(record + 2, id, sizeof id)) return true;
	for (unsigned i = 0; i < record[1]; i++) {
		uint16_t tg = (uint16_t)(record[9 + i * 2] | record[10 + i * 2] << 8);
		if (!tg) { *count = 0; return true; }
		for (unsigned j = 0; j < i; j++) if (out[j] == tg) { *count = 0; return true; }
		out[(*count)++] = tg;
	}
	return true;
}

bool p25_favorites_edit(const channel_t *ch, uint16_t tg, bool remove)
{
	uint16_t groups[P25_MONITOR_FAVORITES];
	uint8_t count, record[FAVORITE_BYTES] = {1};
	if (!tg || !identity(ch, record + 2) || !p25_favorites_read(ch, groups, &count)) return false;
	uint8_t previous[FAVORITE_BYTES];
	int len;
	if (!codeplug_kv_lookup((uint16_t)(CODEPLUG_KEY_P25_FAVORITES + ch->system), previous, sizeof previous, &len) ||
	    (len >= 0 && (len != sizeof previous || previous[0] != 1))) return false;
	unsigned at;
	for (at = 0; at < count && groups[at] != tg; at++) { }
	if (remove) {
		if (at == count) return true;
		memmove(&groups[at], &groups[at + 1], (count - at - 1) * sizeof groups[0]); count--;
	} else if (at == count) {
		if (count == P25_MONITOR_FAVORITES) return false;
		groups[count++] = tg;
	}
	record[1] = count;
	for (unsigned i = 0; i < count; i++) {
		record[9 + i * 2] = (uint8_t)groups[i]; record[10 + i * 2] = (uint8_t)(groups[i] >> 8);
	}
	return codeplug_kv_set((uint16_t)(CODEPLUG_KEY_P25_FAVORITES + ch->system), record, sizeof record);
}
#endif

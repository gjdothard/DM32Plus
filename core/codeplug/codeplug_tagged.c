// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Adapt upstream's tag lifecycle hooks to an explicit API before the radio core understands tagged modes.
#include "config.h"
#if CONFIG_P25
#include "codeplug_tag.h"
#include "storage/storage.h"

bool codeplug_tagged_channel(uint16_t number, channel_t *out)
{
	if (out == NULL) return false;
	storage_lock();
	bool ok = codeplug_channel(number, out);
	if (ok) {
		out->kind = CODEPLUG_KIND_NONE;
		out->system = CODEPLUG_SYSTEM_NONE;
		codeplug_tag_apply(number, out);
	}
	storage_unlock();
	return ok;
}

bool codeplug_tagged_set_channel(uint16_t number, const channel_t *ch)
{
	if (ch == NULL || ch->kind == 0xFFu) return false;
	storage_lock();
	channel_t old, base = *ch;
	bool used = codeplug_source() == CODEPLUG_OPENFW && codeplug_channel(number, &old);
	codeplug_tag_t tag;
	bool safe;
	uint8_t action = codeplug_tag_before_write(number, ch, used, used ? old.name : "", &tag, &safe);
	bool ok = true;
	// Remove an obsolete binding before making a plain, potentially transmitting base live.
	if (action == CODEPLUG_TAG_W_REMOVE) {
		ok = codeplug_tag_remove(number);
		action = CODEPLUG_TAG_W_NONE;
	}
	if (safe) {
		base.mode = RADIO_MODE_DMR;
		base.rx_only = true;
	} else if (used && action == CODEPLUG_TAG_W_FOLLOW && (tag.flags & CODEPLUG_TAG_BASE_OWN)) {
		base.mode = old.mode;
		base.bw = old.bw;
		base.rx_only = old.rx_only;
	}
	if (ok) ok = codeplug_set_channel(number, &base);
	if (ok) ok = codeplug_tag_after_write(number, ch, action, &tag);
	storage_unlock();
	return ok;
}

bool codeplug_tagged_delete_channel(uint16_t number)
{
	storage_lock();
	bool ok = codeplug_tag_remove(number) && codeplug_delete_channel(number);
	storage_unlock();
	return ok;
}
#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Adapt upstream's tag lifecycle hooks to explicit configuration and guarded radio readers.
#include "config.h"
#include "codeplug_tag.h"
#include "codeplug_kv.h"
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
	base.kind = CODEPLUG_KIND_NONE;
	base.system = CODEPLUG_SYSTEM_NONE;
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

bool codeplug_radio_channel(uint16_t number, channel_t *out)
{
	storage_lock();
	bool ok = codeplug_tagged_channel(number, out);
	if (ok && codeplug_source() == CODEPLUG_OPENFW) {
		bool present;
		bool park = !storage_ready() || !codeplug_kv_radio_valid(&present);
		if (!park && present) {
			int len;
			park = !codeplug_kv_lookup((uint16_t)(CODEPLUG_KEY_CH_TAG + number - 1u), NULL, 0, &len) ||
			       (len >= 0 && out->kind == CODEPLUG_KIND_NONE);
		}
		if (park) { out->kind = 0xFFu; out->rx_only = true; }
	}
	storage_unlock();
	return ok;
}

bool codeplug_legacy_write_allowed(void)
{
	storage_lock();
	bool present, tags;
	bool ok = storage_ready() && codeplug_kv_radio_valid(&present);
	if (ok && present) ok = codeplug_kv_range_present(CODEPLUG_KEY_CH_TAG,
		(uint16_t)(CODEPLUG_KEY_CH_TAG + CODEPLUG_CHANNELS_MAX - 1u), &tags) && !tags;
	storage_unlock();
	return ok;
}

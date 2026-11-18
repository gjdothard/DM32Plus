// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Strict stored-channel permission checks; tolerant receive defaults remain in p25_codeplug.c.
#include "config.h"
#if CONFIG_P25
#include <string.h>
#include "p25/p25_admission.h"
#include "p25/p25_codeplug.h"
#include "codeplug/codeplug_kv.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_records.h"
#include "storage/storage.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }
static bool unit_ok(uint32_t id) { return id >= 1u && id <= P25_CP_UNIT_MAX; }

static bool nac_ok(uint16_t value, bool tx)
{
	if (value == 0u) return true;
	if ((value & 0xF000u) != P25_CP_NAC_SET) return false;
	value &= 0xFFFu;
	return value != P25_NAC_REPEAT_ANY && (!tx || value != P25_NAC_ANY);
}

// Compare fields that affect P25 operation, without comparing struct padding or unrelated DMR settings.
static bool same_channel(const channel_t *a, const channel_t *b)
{
	return strncmp(a->name, b->name, sizeof a->name) == 0 && a->kind == b->kind && a->system == b->system &&
	       a->rx_hz == b->rx_hz && a->tx_hz == b->tx_hz && a->rx_only == b->rx_only && a->dmr_id == b->dmr_id &&
	       a->contact == b->contact && a->listen_list == b->listen_list && a->dmo == b->dmo &&
	       a->privacy_slot == b->privacy_slot;
}

static bool identity_ok(const channel_t *ch, uint32_t own)
{
	if (ch->dmr_id != 0u) return unit_ok(ch->dmr_id);
	if (own != 0u) return unit_ok(own);
	uint8_t raw[4];
	int len;
	if (!codeplug_kv_lookup(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, raw, sizeof raw, &len)) return false;
	if (len >= 0) {
		if (len != (int)sizeof raw) return false;
		uint32_t id = rd32(raw);
		if (id != 0u) return unit_ok(id);
	}
	return unit_ok(codeplug_dmr_id());
}

static bool target_ok(const channel_t *ch, bool trunk)
{
	contact_t c;
	if (ch->contact != 0u) {
		if (!codeplug_contact(ch->contact, &c)) return false;
	} else if (ch->listen_list != 0u) {
		listen_list_t list;
		if (!codeplug_listen_list(ch->listen_list, &list)) return false;
		bool found = false;
		for (uint8_t i = 0; i < list.count; i++) {
			if (!codeplug_contact(list.contacts[i], &c)) return false;
			if (c.type != CONTACT_PRIVATE) {
				found = true;
				break;
			}
		}
		if (!found) return false;
	} else {
		return true; // explicit default talkgroup 1
	}
	if (c.type == CONTACT_GROUP) return c.id >= 1u && c.id <= 0xFFFFu;
	if (c.type == CONTACT_ALL_CALL) return true;
	return !trunk && c.type == CONTACT_PRIVATE && unit_ok(c.id);
}

static uint8_t permissions(uint16_t number, const channel_t *active)
{
	if (codeplug_source() != CODEPLUG_OPENFW) return 0;
	channel_t ch;
	codeplug_tag_t tag;
	// Resolve the base and binding once; a second channel copy would consume task stack and repeat flash reads.
	if (!codeplug_channel(number, &ch) || !codeplug_tag_read(number, &tag) ||
	    !codeplug_tag_live(&tag, ch.name, ch.mode, ch.rx_only) || (tag.flags & CODEPLUG_TAG_TX) == 0) return 0;
	ch.kind = tag.kind;
	ch.system = tag.system;
	ch.rx_only = false;
	if (!same_channel(&ch, active)) return 0;
	uint8_t allowed_flags = CODEPLUG_TAG_TX | CODEPLUG_TAG_BASE_OWN | CODEPLUG_TAG_FALLBACK;
#if CONFIG_P25_TRUNK
	if (ch.kind == CODEPLUG_KIND_P25_TRUNK) allowed_flags |= P25_TAG_NO_FALLBACK;
#endif
	if ((tag.flags & ~allowed_flags) != 0) return 0;
	uint8_t raw[P25_CP_CONV_LEN + CODEPLUG_TAG_HEAD_MAX];
	int len = codeplug_tag_params(number, &ch, raw, P25_CP_CONV_LEN);
	if (ch.kind == CODEPLUG_KIND_P25) {
		if (ch.system != CODEPLUG_SYSTEM_NONE || len != P25_CP_CONV_LEN || raw[0] > P25_SQUELCH_SELECTIVE ||
		    !nac_ok(rd16(raw + 1), false) || !nac_ok(rd16(raw + 3), true) ||
		    !identity_ok(&ch, 0) || !target_ok(&ch, false)) return 0;
		return P25_TX_VOICE;
	}
#if CONFIG_P25_TRUNK
	if (ch.kind == CODEPLUG_KIND_P25_TRUNK) {
		if (ch.system >= CODEPLUG_SYSTEMS_RUN || len != 1 || (raw[0] & ~P25_TCH_FOLLOW_ALL) != 0) return 0;
		uint8_t block[CODEPLUG_SYS_HDR + P25_CP_TSYS_LEN];
		codeplug_system_t sy;
		len = codeplug_system(ch.system, &sy, block, P25_CP_TSYS_LEN);
		if (len != P25_CP_TSYS_LEN || sy.type != CODEPLUG_SYSTYPE_P25_TRUNK || sy.flags != CODEPLUG_SYS_TX ||
		    sy.sharing > CODEPLUG_SHARE_WHOLE || sy.id_len != 5 || sy.own_id > P25_CP_UNIT_MAX) return 0;
		uint32_t wacn = (uint32_t)rd16(block) | ((uint32_t)block[2] << 16);
		uint16_t sysid = rd16(block + 3);
		if ((wacn > 0xFFFFFu && wacn != P25_CP_WACN_ANY) || (sysid > 0xFFFu && sysid != P25_CP_SYSID_ANY) ||
		    !nac_ok(rd16(block + 5), false) ||
		    (block[7] & ~(P25_TSYS_SKIP_ENC | P25_TSYS_PLAN_ONLY | P25_TSYS_NO_REG)) != 0 ||
		    !identity_ok(&ch, sy.own_id) || !target_ok(&ch, true)) return 0;
		return P25_TX_VOICE | P25_TX_REQUEST | ((block[7] & P25_TSYS_NO_REG) ? 0u : P25_TX_REGISTRATION);
	}
#endif
	return 0;
}

uint8_t p25_codeplug_tx_permissions(uint16_t number, const channel_t *active)
{
	if (active == NULL) return 0;
	storage_lock();
	uint8_t result = permissions(number, active);
	storage_unlock();
	return result;
}
#endif

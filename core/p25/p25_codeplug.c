// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// Parameter codecs from the pinned upstream; storage/tag loaders await their generic backend.
#include <string.h>
#include "config.h"
#include "p25_codeplug.h"
#if CONFIG_P25
static uint16_t rd16(const uint8_t *b, int len, int at)
{
	return at + 2 <= len ? (uint16_t)(b[at] | (b[at + 1] << 8)) : 0u;
}

static uint32_t rd32(const uint8_t *b, int len, int at)
{
	return at + 4 <= len ? (uint32_t)rd16(b, len, at) | ((uint32_t)rd16(b, len, at + 2) << 16) : 0u;
}

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// A NAC field: its value when set, else dflt. A value the field can't hold reads as dflt too
static uint16_t nac_field(uint16_t f, uint16_t dflt)
{
	return (f & P25_CP_NAC_SET) != 0 && (f & 0x7000u) == 0 ? (uint16_t)(f & 0x0FFFu) : dflt;
}

void p25_conv_decode(const uint8_t *b, int len, p25_conv_t *out)
{
	uint8_t sq = len >= 1 ? (uint8_t)(b[0] & 0x03u) : 0u;
	out->squelch = sq == P25_SQUELCH_SELECTIVE ? P25_SQUELCH_SELECTIVE : P25_SQUELCH_NORMAL;
	uint16_t rx = nac_field(rd16(b, len, 1), P25_NAC_DEFAULT);
	if (rx == P25_NAC_REPEAT_ANY) {
		rx = P25_NAC_DEFAULT;		// a repeater's value, never a radio's
	}
	uint16_t tx_dflt = rx == P25_NAC_ANY ? P25_NAC_DEFAULT : rx;
	uint16_t tx = nac_field(rd16(b, len, 3), tx_dflt);
	if (tx == P25_NAC_ANY || tx == P25_NAC_REPEAT_ANY) {
		tx = tx_dflt;			// "any" is no value to send
	}
	out->rx_nac = rx;
	out->tx_nac = tx;
}

void p25_conv_encode(const p25_conv_t *c, uint8_t out[P25_CP_CONV_LEN])
{
	out[0] = c->squelch == P25_SQUELCH_SELECTIVE ? P25_SQUELCH_SELECTIVE : P25_SQUELCH_NORMAL;
	uint16_t tx_dflt = c->rx_nac == P25_NAC_ANY ? P25_NAC_DEFAULT : c->rx_nac;
	wr16(&out[1], c->rx_nac == P25_NAC_DEFAULT ? 0u : (uint16_t)(P25_CP_NAC_SET | (c->rx_nac & 0x0FFFu)));
	wr16(&out[3], c->tx_nac == tx_dflt ? 0u : (uint16_t)(P25_CP_NAC_SET | (c->tx_nac & 0x0FFFu)));
}

uint32_t p25_unit_id_decode(const uint8_t *b, int len, uint32_t dmr_id)
{
	uint32_t id = len >= 4 ? rd32(b, len, 0) : 0u;
	return id >= 1u && id <= P25_CP_UNIT_MAX ? id : dmr_id;
}

#if CONFIG_P25_TRUNK
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

void p25_tsys_decode(const uint8_t *b, int len, p25_tsys_t *out)
{
	uint32_t w = len >= 3 ? (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) : 0u;
	out->wacn = w == P25_CP_WACN_ANY ? w : (w & 0xFFFFFu);
	uint16_t s = rd16(b, len, 3);
	out->sysid = s == P25_CP_SYSID_ANY ? s : (uint16_t)(s & 0x0FFFu);
	out->cc_nac = nac_field(rd16(b, len, 5), P25_NAC_ANY);
	out->options = len >= 8 ? (uint8_t)(b[7] & (P25_TSYS_SKIP_ENC | P25_TSYS_PLAN_ONLY | P25_TSYS_NO_REG)) : 0u;
	out->hold_s = len >= 9 ? b[8] : 0u;
	out->roam_dbm = len >= 10 ? (int8_t)b[9] : 0;
}

void p25_tsys_encode(const p25_tsys_t *s, uint8_t out[P25_CP_TSYS_LEN])
{
	out[0] = (uint8_t)s->wacn;
	out[1] = (uint8_t)(s->wacn >> 8);
	out[2] = (uint8_t)(s->wacn >> 16);
	wr16(&out[3], s->sysid);
	wr16(&out[5], s->cc_nac == P25_NAC_ANY ? 0u : (uint16_t)(P25_CP_NAC_SET | (s->cc_nac & 0x0FFFu)));
	out[7] = s->options;
	out[8] = s->hold_s;
	out[9] = (uint8_t)s->roam_dbm;
}

void p25_site_decode(const uint8_t *b, int len, p25_site_t *out)
{
	out->rfss = len >= 1 ? b[0] : P25_CP_UNKNOWN;
	out->site = len >= 2 ? b[1] : P25_CP_UNKNOWN;
}

void p25_ctl_decode(const uint8_t *b, int len, p25_ctl_t *out)
{
	out->chan = rd16(b, len, 0);
	out->rx_hz = rd32(b, len, 2);
	out->tx_hz = rd32(b, len, 6);
}

void p25_iden_decode(const uint8_t *b, int len, p25_iden_t *out)
{
	out->base_hz = rd32(b, len, 0);
	out->spacing_hz = rd32(b, len, 4);
	out->tx_offset_hz = (int32_t)rd32(b, len, 8);
	out->bw_hz = rd32(b, len, 12);
	out->type = len >= 17 && b[16] == P25_IDEN_TDMA ? P25_IDEN_TDMA : P25_IDEN_FDMA;
	out->slots = out->type == P25_IDEN_TDMA ? (len >= 18 && b[17] != 0u ? b[17] : 2u) : 1u;
}

void p25_iden_encode(const p25_iden_t *i, uint8_t out[P25_CP_IDEN_LEN])
{
	wr32(&out[0], i->base_hz);
	wr32(&out[4], i->spacing_hz);
	wr32(&out[8], (uint32_t)i->tx_offset_hz);
	wr32(&out[12], i->bw_hz);
	out[16] = i->type;
	out[17] = i->type == P25_IDEN_TDMA ? i->slots : 0u;
}

#endif
#endif

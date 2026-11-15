// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AES follows FIPS 197. Voice framing follows docs/spec/dmr-voice.md; no vendor code or codec bytes.
#include "config.h"
#if CONFIG_DMR_AES256
#include <string.h>
#include "dmr/dmrAes.h"
#include "dmr/dmrPrivacy.h"
#include "dmr/ambe/ambeFec.h"

static const uint8_t sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static void wipe(void *p, unsigned n)
{
	volatile uint8_t *v = p;
	while (n--)
		*v++ = 0;
}

static uint8_t xtime(uint8_t x)
{
	return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1Bu));
}

void dmrAesBlock(const uint8_t key[32], const uint8_t in[16], uint8_t out[16])
{
	uint8_t k[32], a[16], b[16], rc = 1;
	memcpy(k, key, 32);
	for (unsigned i = 0; i < 16; i++)
		a[i] = in[i] ^ k[i];
	for (unsigned round = 1; round <= 14; round++) {
		if ((round & 1u) == 0) {
			// A rolling eight-word key schedule avoids keeping 240 bytes per direction in RAM.
			uint8_t t[4];
			for (unsigned i = 0; i < 32; i += 4) {
				memcpy(t, k + ((i + 28) & 31), 4);
				if (i == 0) {
					uint8_t first = t[0];
					t[0] = sbox[t[1]] ^ rc;
					t[1] = sbox[t[2]];
					t[2] = sbox[t[3]];
					t[3] = sbox[first];
					rc = xtime(rc);
				} else if (i == 16) {
					for (unsigned j = 0; j < 4; j++)
						t[j] = sbox[t[j]];
				}
				for (unsigned j = 0; j < 4; j++)
					k[i + j] ^= t[j];
			}
			wipe(t, sizeof(t));
		}
		for (unsigned col = 0; col < 4; col++)
			for (unsigned row = 0; row < 4; row++)
				b[4 * col + row] = sbox[a[4 * ((col + row) & 3u) + row]];
		if (round != 14) {
			for (unsigned i = 0; i < 16; i += 4) {
				uint8_t t = b[i] ^ b[i + 1] ^ b[i + 2] ^ b[i + 3];
				uint8_t first = b[i];
				b[i] ^= t ^ xtime(b[i] ^ b[i + 1]);
				b[i + 1] ^= t ^ xtime(b[i + 1] ^ b[i + 2]);
				b[i + 2] ^= t ^ xtime(b[i + 2] ^ b[i + 3]);
				b[i + 3] ^= t ^ xtime(b[i + 3] ^ first);
			}
		}
		for (unsigned i = 0; i < 16; i++)
			a[i] = b[i] ^ k[(round & 1u) * 16 + i];
	}
	memcpy(out, a, 16);
	wipe(k, sizeof(k));
	wipe(a, sizeof(a));
	wipe(b, sizeof(b));
}

uint32_t dmrAesAdvance(uint32_t mi)
{
	for (unsigned i = 0; i < 32; i++)
		mi = (mi << 1) | (((mi >> 31) ^ (mi >> 21) ^ (mi >> 1) ^ mi) & 1u);
	return mi;
}

static void be32(uint8_t *p, uint32_t v)
{
	for (unsigned i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (24 - 8 * i));
}

void dmrAesStream(const uint8_t key[32], uint32_t mi, uint8_t out[126])
{
	uint8_t block[16];
	for (unsigned i = 0; i < 4; i++) {
		be32(block + 4 * i, mi);
		mi = dmrAesAdvance(mi);
	}
	dmrAesBlock(key, block, block);	// the first OFB block is not used for voice
	for (unsigned n = 0; n < 126; n += 16) {
		dmrAesBlock(key, block, block);
		unsigned take = 126 - n < 16 ? 126 - n : 16;
		memcpy(out + n, block, take);
	}
	wipe(block, sizeof(block));
}

static uint8_t crc4(uint32_t mi)
{
	uint8_t r = 0;
	for (int bit = 31; bit >= 0; bit--) {
		uint8_t feedback = (uint8_t)(((r >> 3) ^ (mi >> bit)) & 1u);
		r = (uint8_t)(((r << 1) & 15u) ^ (feedback * 3u));
	}
	return r ^ 15u;
}

static void late_encode(uint32_t mi, uint32_t words[3])
{
	words[0] = ambeGolay24Encode((uint16_t)(mi >> 20));
	words[1] = ambeGolay24Encode((uint16_t)((mi >> 8) & 0xFFFu));
	words[2] = ambeGolay24Encode((uint16_t)(((mi & 255u) << 4) | crc4(mi)));
}

static bool late_decode(const uint32_t words[3], uint32_t *mi)
{
	uint16_t data[3];
	for (unsigned f = 0; f < 3; f++)
		if (ambeGolay24Decode(words[f], &data[f]) < 0)
			return false;
	uint32_t v = ((uint32_t)data[0] << 20) | ((uint32_t)data[1] << 8) | (data[2] >> 4);
	if (v == 0 || crc4(v) != (data[2] & 15u))
		return false;
	*mi = v;
	return true;
}

static void rx_reset(void *ctx)
{
	dmr_aes_t *c = ctx;
	c->rx_valid = c->rx_rejected = c->rx_have_index = c->rx_have_pending = false;
	c->rx_mask = 0;
	c->rx_slot = c->rx_mi = c->rx_pending = 0;
	memset(c->rx_le, 0, sizeof(c->rx_le));
	wipe(c->rx_stream, sizeof(c->rx_stream));
}

void dmrAesClear(dmr_aes_t *c)
{
	wipe(c, sizeof(*c));
}

bool dmrAesLoad(dmr_aes_t *c, const uint8_t key[32], uint8_t key_id, uint32_t seed)
{
	if (!key || !key_id || !seed)
		return false;
	dmrAesClear(c);
	memcpy(c->key, key, 32);
	c->key_id = key_id;
	c->tx_next = seed;
	c->remaining = 65536u;	// bounded session; a new host seed is required when exhausted
	c->loaded = true;
	return true;
}

static bool pi_rx(void *ctx, const uint8_t pi[12], uint32_t dst)
{
	dmr_aes_t *c = ctx;
	rx_reset(c);
	uint32_t target = ((uint32_t)pi[7] << 16) | ((uint32_t)pi[8] << 8) | pi[9];
	uint32_t mi = ((uint32_t)pi[3] << 24) | ((uint32_t)pi[4] << 16) | ((uint32_t)pi[5] << 8) | pi[6];
	c->rx_rejected = !c->loaded || pi[0] != 0x25 || pi[1] != 0x10 || pi[2] != c->key_id ||
		target != dst || mi == 0;
	if (c->rx_rejected)
		return false;
	c->rx_mi = mi;
	dmrAesStream(c->key, mi, c->rx_stream);
	c->rx_valid = true;
	return true;
}

static bool tx_prepare(dmr_aes_t *c)
{
	if (!c->loaded || !c->remaining || !c->tx_next)
		return false;
	uint32_t mi = c->tx_next;
	c->tx_next = dmrAesAdvance(mi);
	c->remaining--;
	dmrAesStream(c->key, mi, c->tx_stream);
	late_encode(c->tx_next, c->tx_le);
	return true;
}

static bool tx_begin(void *ctx, uint32_t dst, uint8_t pi[12])
{
	dmr_aes_t *c = ctx;
	uint32_t mi = c->tx_next;
	if (!tx_prepare(c))
		return false;
	memset(pi, 0, 12);
	pi[0] = 0x25;
	pi[1] = 0x10;
	pi[2] = c->key_id;
	be32(pi + 3, mi);
	pi[7] = (uint8_t)(dst >> 16);
	pi[8] = (uint8_t)(dst >> 8);
	pi[9] = (uint8_t)dst;
	uint16_t crc = dmr_crc_ccitt(pi, 10) ^ DMR_CRC_MASK_PI;
	pi[10] = (uint8_t)(crc >> 8);
	pi[11] = (uint8_t)crc;
	c->tx_last = 6;
	return true;
}

static void transform(const uint8_t stream[126], uint8_t burst[27], uint8_t index, bool rx)
{
	for (unsigned f = 0; f < 3; f++) {
		uint8_t bits[49], *frame = burst + f * 9;
		if (rx)
			ambeFecDecode(frame, bits, NULL);
		else
			dmrPrivacyAmbeExtract(frame, bits);
		unsigned start = (index * 3u + f) * 7u;
		for (unsigned i = 0; i < 49; i++)
			bits[i] ^= (stream[start + i / 8] >> (7 - i % 8)) & 1u;
		ambeFecEncode(bits, frame);
		wipe(bits, sizeof(bits));
	}
}

static bool tx_burst(void *ctx, uint8_t burst[27], uint8_t index)
{
	dmr_aes_t *c = ctx;
	if (index >= 6 || !c->loaded)
		return false;
	if (index == 0 && c->tx_last == 5 && !tx_prepare(c))
		return false;
	transform(c->tx_stream, burst, index, false);
	for (unsigned f = 0; f < 3; f++) {
		uint8_t n = (uint8_t)((c->tx_le[f] >> (20 - index * 4)) & 15u);
		uint8_t *p = burst + f * 9;
		p[7] = (uint8_t)((p[7] & ~0x11u) | ((n & 1u) << 4) | ((n >> 1) & 1u));
		p[8] = (uint8_t)((p[8] & ~0x11u) | (((n >> 2) & 1u) << 4) | ((n >> 3) & 1u));
	}
	c->tx_last = index;
	return true;
}

static bool rx_burst(void *ctx, uint8_t burst[27], uint8_t index, uint32_t slot)
{
	dmr_aes_t *c = ctx;
	if (index >= 6 || !c->loaded || c->rx_rejected)
		return false;
	unsigned advance = 0;
	if (c->rx_have_index) {
		advance = index <= c->rx_last ? 1u : 0u;
		if (slot && c->rx_slot)
			advance = (unsigned)((c->rx_last + (uint32_t)(slot - c->rx_slot) / 2u) / 6u);
	}
	if (advance) {
		if (advance > 64) {
			c->rx_valid = false;	// a long gap: wait for a complete late-entry MI
			c->rx_have_pending = false;
		} else {
			while (advance--) {
				if (c->rx_have_pending) {
					c->rx_mi = c->rx_pending;
					c->rx_valid = true;
					c->rx_have_pending = false;
				} else if (c->rx_valid) {
					c->rx_mi = dmrAesAdvance(c->rx_mi);
				}
			}
			if (c->rx_valid)
				dmrAesStream(c->key, c->rx_mi, c->rx_stream);
		}
		c->rx_mask = 0;
		memset(c->rx_le, 0, sizeof(c->rx_le));
	}
	c->rx_have_index = true;
	c->rx_last = index;
	c->rx_slot = slot;
	if (index == 0) {
		c->rx_mask = 0;
		memset(c->rx_le, 0, sizeof(c->rx_le));
	}
	for (unsigned f = 0; f < 3; f++) {
		const uint8_t *p = burst + f * 9;
		uint32_t nibble = ((p[7] >> 4) & 1u) | ((p[7] & 1u) << 1) |
			(((p[8] >> 4) & 1u) << 2) | ((p[8] & 1u) << 3);
		c->rx_le[f] |= nibble << (20 - 4 * index);
	}
	c->rx_mask |= (uint8_t)(1u << index);
	if (index == 5 && c->rx_mask == 0x3F && late_decode(c->rx_le, &c->rx_pending))
		c->rx_have_pending = true;
	if (!c->rx_valid)
		return false;
	transform(c->rx_stream, burst, index, true);
	return true;
}

const dmr_privacy_ops_t dmr_aes_ops = { rx_reset, pi_rx, rx_burst, tx_begin, tx_burst };
#endif

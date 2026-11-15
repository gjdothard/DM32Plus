// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// FIPS AES answer, synthetic stock-compatible OFB vector, and voice/MI loss recovery. No radio image needed.
#include <stdio.h>
#include <string.h>
#include "dmr/dmrAes.h"
#include "dmr/ambe/ambeFec.h"

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void voice(uint8_t burst[27], unsigned salt)
{
	for (unsigned f = 0; f < 3; f++) {
		uint8_t bits[49];
		for (unsigned i = 0; i < 49; i++)
			bits[i] = (uint8_t)((i * 7 + i / 3 + salt + f) & 1);
		ambeFecEncode(bits, burst + f * 9);
	}
}

static void same_voice(const uint8_t a[27], const uint8_t b[27])
{
	for (unsigned f = 0; f < 3; f++) {
		uint8_t aa[49], bb[49];
		ambeFecDecode(a + f * 9, aa, NULL);
		ambeFecDecode(b + f * 9, bb, NULL);
		// The low four C3 bits carry late-entry signaling in this format.
		CHECK(memcmp(aa, bb, 45) == 0);
	}
}

int main(void)
{
	uint8_t key[32], block[16], stream[126];
	for (unsigned i = 0; i < 32; i++) key[i] = (uint8_t)i;
	const uint8_t plain[16] = { 0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF };
	const uint8_t cipher[16] = { 0x8E,0xA2,0xB7,0xCA,0x51,0x67,0x45,0xBF,0xEA,0xFC,0x49,0x90,0x4B,0x49,0x60,0x89 };
	dmrAesBlock(key, plain, block);
	CHECK(memcmp(block, cipher, 16) == 0);
	const uint8_t known[16] = { 0x3C,0x30,0xD1,0xA3,0x37,0xDD,0x05,0x5D,0x10,0x32,0xB6,0xDD,0x22,0x26,0x2C,0x8A };
	dmrAesStream(key, 0x12345678u, stream);
	CHECK(memcmp(stream, known, 16) == 0);
	const uint8_t suffix[16] = { 0xC4,0x2C,0xEC,0x30,0x0C,0x53,0xDD,0x47,0x4C,0xD8,0xFC,0x2E,0xF1,0xA7,0x9F,0x67 };
	CHECK(memcmp(stream + 110, suffix, 16) == 0);
	CHECK(dmrAesAdvance(0x12345678u) == 0xB451463Au);
	key[31] ^= 1;
	dmrAesBlock(key, plain, block);
	CHECK(memcmp(block, cipher, 16) != 0);
	key[31] ^= 1;

	dmr_aes_t tx, rx;
	uint8_t pi[12], burst[27], original[27], saved[6][27];
	CHECK(!dmrAesLoad(&tx, key, 0, 1));
	CHECK(!dmrAesLoad(&tx, key, 7, 0));
	CHECK(dmrAesLoad(&tx, key, 7, 0x12345678u));
	CHECK(dmrAesLoad(&rx, key, 7, 1));
	CHECK(dmr_aes_ops.begin(&tx, 91, pi));
	CHECK(pi[0] == 0x25 && pi[1] == 0x10 && pi[2] == 7 && pi[3] == 0x12 && pi[9] == 91);
	CHECK((((uint16_t)pi[10] << 8) | pi[11]) == (dmr_crc_ccitt(pi, 10) ^ DMR_CRC_MASK_PI));
	CHECK(dmr_aes_ops.pi(&rx, pi, 91));
	for (unsigned n = 0; n < 24; n++) {
		voice(original, n);
		memcpy(burst, original, 27);
		CHECK(dmr_aes_ops.tx(&tx, burst, n % 6));
		if (n % 6 == 0) {
			uint8_t retry[27];
			uint32_t next = tx.tx_next, remaining = tx.remaining;
			memcpy(retry, original, 27);
			CHECK(dmr_aes_ops.tx(&tx, retry, 0));
			CHECK(memcmp(retry, burst, 27) == 0 && next == tx.tx_next && remaining == tx.remaining);
		}
		if (n < 6) memcpy(saved[n], burst, 27);
		// Skip a burst, then an entire superframe. Slot counters disambiguate the same index a frame later.
		if (n == 2 || (n >= 6 && n < 12)) continue;
		CHECK(dmr_aes_ops.rx(&rx, burst, n % 6, 100 + 2 * n));
		same_voice(original, burst);
	}

	// Rejoining without the PI: no audio before all six late-entry fragments, next A uses the recovered MI.
	dmr_aes_ops.rx_reset(&rx);
	for (unsigned i = 0; i < 6; i++) {
		memcpy(burst, saved[i], 27);
		CHECK(!dmr_aes_ops.rx(&rx, burst, i, 100 + i * 2));
	}
	CHECK(rx.rx_have_pending && rx.rx_pending == 0xB451463Au);
	dmrAesLoad(&tx, key, 7, 0xB451463Au);
	CHECK(dmr_aes_ops.begin(&tx, 91, pi));
	voice(original, 5);
	memcpy(burst, original, 27);
	CHECK(dmr_aes_ops.tx(&tx, burst, 0));
	CHECK(dmr_aes_ops.rx(&rx, burst, 0, 112));
	same_voice(original, burst);

	// A stale pending MI cannot restore playback after a gap outside the recovery window.
	rx.rx_have_pending = true;
	rx.rx_pending = 1;
	memcpy(burst, saved[1], 27);
	CHECK(!dmr_aes_ops.rx(&rx, burst, 1, 10000));
	CHECK(!rx.rx_valid && !rx.rx_have_pending);
	memcpy(burst, saved[2], 27);
	CHECK(!dmr_aes_ops.rx(&rx, burst, 2, 10002));

	// Observed stock late-entry words use different parity for some inputs; standard decoding corrects this vector.
	const uint32_t stock_words[3] = { 0x1230ACu, 0x45696Du, 0x789810u };
	dmr_aes_ops.rx_reset(&rx);
	for (unsigned i = 0; i < 6; i++) {
		voice(burst, i);
		for (unsigned f = 0; f < 3; f++) {
			uint8_t n = (uint8_t)((stock_words[f] >> (20 - i * 4)) & 15u);
			uint8_t *p = burst + f * 9;
			p[7] = (uint8_t)((p[7] & ~0x11u) | ((n & 1u) << 4) | ((n >> 1) & 1u));
			p[8] = (uint8_t)((p[8] & ~0x11u) | (((n >> 2) & 1u) << 4) | ((n >> 3) & 1u));
		}
		CHECK(!dmr_aes_ops.rx(&rx, burst, i, 200 + i * 2));
	}
	CHECK(rx.rx_have_pending && rx.rx_pending == 0x12345678u);

	// Explicit wrong algorithm, key ID, manufacturer, or destination must keep late entry muted too.
	for (unsigned field = 0; field < 4; field++) {
		uint8_t bad[12];
		memcpy(bad, pi, 12);
		bad[field == 3 ? 9 : field] ^= 1;
		CHECK(!dmr_aes_ops.pi(&rx, bad, 91));
		for (unsigned i = 0; i < 6; i++) {
			memcpy(burst, saved[i], 27);
			CHECK(!dmr_aes_ops.rx(&rx, burst, i, 200 + i * 2));
		}
	}
	CHECK(!dmr_aes_ops.tx(&tx, burst, 6));
	tx.remaining = 0;
	CHECK(!dmr_aes_ops.begin(&tx, 91, pi));
	dmrAesClear(&tx);
	CHECK(!tx.loaded);
	for (unsigned i = 0; i < sizeof(tx); i++) CHECK(((const uint8_t *)&tx)[i] == 0);
	printf("dmrAesTest: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}

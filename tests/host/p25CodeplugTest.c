// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// The upstream codec vector tests, isolated from the not-yet-ported storage backend.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p25/p25_codeplug.h"
static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static int unhex(const char *s, uint8_t *out, int max)
{
	if (strcmp(s, "-") == 0) {
		return 0;
	}
	int n = 0;
	while (s[0] != '\0' && s[1] != '\0' && n < max) {
		char b[3] = { s[0], s[1], 0 };
		out[n++] = (uint8_t)strtoul(b, NULL, 16);
		s += 2;
	}
	return n;
}

static uint32_t h(const char *s) { return (uint32_t)strtoul(s, NULL, 16); }

// The shared vectors, each kind against the codec
static void test_vectors(void)
{
	FILE *f = fopen("../vectors/p25-codeplug.txt", "r");
	CHECK(f != NULL);
	if (f == NULL) {
		return;
	}
	char line[256];
	int counted = 0;
	while (fgets(line, sizeof line, f) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}
		char *w[10] = { 0 };
		int n = 0;
		for (char *p = strtok(line, " "); p != NULL && n < 10; p = strtok(NULL, " ")) {
			w[n++] = p;
		}
		uint8_t b[64];
		if (strcmp(w[0], "p25conv") == 0) {
			p25_conv_t c;
			p25_conv_decode(b, unhex(w[1], b, sizeof b), &c);
			CHECK(c.rx_nac == h(w[2]) && c.tx_nac == h(w[3]) && c.squelch == h(w[4]));
		} else if (strcmp(w[0], "p25enc") == 0) {
			p25_conv_t c = { (uint16_t)h(w[1]), (uint16_t)h(w[2]), (uint8_t)h(w[3]) };
			uint8_t e[P25_CP_CONV_LEN];
			p25_conv_encode(&c, e);
			CHECK(unhex(w[4], b, sizeof b) == P25_CP_CONV_LEN && memcmp(e, b, P25_CP_CONV_LEN) == 0);
		} else if (strcmp(w[0], "p25unit") == 0) {
			int len = unhex(w[1], b, sizeof b);
			CHECK(p25_unit_id_decode(b, len == 0 ? -1 : len, h(w[2])) == h(w[3]));
#if CONFIG_P25_TRUNK
		} else if (strcmp(w[0], "p25tsys") == 0) {
			p25_tsys_t s;
			p25_tsys_decode(b, unhex(w[1], b, sizeof b), &s);
			CHECK(s.wacn == h(w[2]) && s.sysid == h(w[3]) && s.cc_nac == h(w[4]) && s.options == h(w[5]) &&
			      s.hold_s == h(w[6]) && (uint8_t)s.roam_dbm == h(w[7]));
		} else if (strcmp(w[0], "p25iden") == 0) {
			p25_iden_t i;
			int len = unhex(w[1], b, sizeof b);
			p25_iden_decode(b, len, &i);
			CHECK(i.base_hz == h(w[2]) && i.spacing_hz == h(w[3]) && (uint32_t)i.tx_offset_hz == h(w[4]) &&
			      i.bw_hz == h(w[5]) && i.type == h(w[6]) && i.slots == h(w[7]));
			if (len == (int)P25_CP_IDEN_LEN && b[17] != 0) {
				uint8_t e[P25_CP_IDEN_LEN];
				p25_iden_encode(&i, e);
				CHECK(memcmp(e, b, P25_CP_IDEN_LEN) == 0);
			}
#endif
		} else {
			continue;
		}
		counted++;
	}
	fclose(f);
	CHECK(counted == (CONFIG_P25_TRUNK ? 39 : 28));
}
int main(void)
{
	test_vectors();
	printf("p25CodeplugTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

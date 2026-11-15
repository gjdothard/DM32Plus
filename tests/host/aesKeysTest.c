// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include <stdio.h>
#include <string.h>
#include "dmr/aesKeys.h"
#include "dmr/dmrAes.h"
#include "storage/storage.h"
#include "sim/sim_flash.h"

static unsigned failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
void storage_lock(void) {}
void storage_unlock(void) {}
void storage_quick_lock(void) {}
void storage_quick_unlock(void) {}
static const hal_flash_map_t map = {
	.settings_addr = 0x1000, .settings_size = 0x8000, .journal_addr = 0x9000,
	.openfw_eeprom = 0xB000, .openfw_flash = 0x10000, .openfw_end = 0x20000,
	.native_addr = 0x20000, .native_size = 0x10000,
};
const board_t board = { .name = "test", .flash = &sim_flash };

int main(void)
{
	uint8_t key[32], got[32], ids[AES_KEY_SLOTS], active, id;
	uint32_t mi, second;
	memset(key, 0x42, sizeof(key));
	sim_flash_create(0x40000, &map);
	CHECK(storage_init());
	CHECK(aesKeysList(ids, &active) && active == 0 && ids[0] == 0);
	CHECK(!aesKeysPut(0, 7, key, 1));
	CHECK(!aesKeysPut(1, 0, key, 1));
	CHECK(!aesKeysPut(1, 7, key, 0));
	CHECK(aesKeysPut(1, 7, key, 0x12345678));
	CHECK(!aesKeysPut(2, 7, key, 1));
	CHECK(!aesKeysPut(2, 8, key, 1));
	CHECK(aesKeysSelect(1, got, &id, &mi));
	CHECK(id == 7 && mi == 0x12345678 && !memcmp(key, got, 32));
	CHECK(aesKeysList(ids, &active) && active == 1 && ids[0] == 7);
	CHECK(storage_init()); // a new boot retains the key but burns a fresh range before returning it
	CHECK(aesKeysRestore(got, &id, &second) == 1);
	uint32_t expected = mi;
	for (unsigned i = 0; i < 65536; i++)
		expected = dmrAesAdvance(expected);
	CHECK(second == expected && second != mi && !memcmp(got, key, 32));
	CHECK(aesKeysPut(1, 7, key, 99)); // reprogramming the same key must preserve its MI allocation
	CHECK(aesKeysRestore(got, &id, &mi) == 1 && mi != second && mi != 99);
	CHECK(aesKeysSelect(0, got, &id, &mi));
	CHECK(!aesKeysRestore(got, &id, &mi));
	CHECK(aesKeysAcquire(1, got, &id, &mi) && id == 7 && !memcmp(got, key, 32));
	CHECK(aesKeysList(ids, &active) && active == 0);
	CHECK(aesKeysAcquire(1, got, &id, &second) && second != mi);
	CHECK(aesKeysList(ids, &active) && active == 0);
	CHECK(!aesKeysAcquire(0, got, &id, &mi) && !aesKeysAcquire(2, got, &id, &mi));
	for (unsigned i = 0; i < 80; i++)
		CHECK(aesKeysSelect(1, got, &id, &mi));
	CHECK(sim_flash_erases(0x2E000) > 0 && sim_flash_erases(0x2F000) > 0);
	CHECK(!sim_flash_violations());
	CHECK(aesKeysDelete(1));
	CHECK(aesKeysList(ids, &active) && active == 0 && ids[0] == 0);
	CHECK(!aesKeysRestore(got, &id, &mi));

	// Failed commits must never return a usable key/range. On restart, either the old or new complete bank survives.
	for (unsigned cut = 1; cut <= 260; cut++) {
		sim_flash_free();
		sim_flash_create(0x40000, &map);
		CHECK(storage_init());
		CHECK(aesKeysPut(1, 7, key, 0x12345678));
		CHECK(aesKeysSelect(1, got, &id, &mi));
		sim_flash_cut_after(cut);
		bool saved = aesKeysSelect(1, got, &id, &second);
		sim_flash_power_on();
		CHECK(storage_init());
		uint32_t restored;
		CHECK(aesKeysRestore(got, &id, &restored) == 1);
		CHECK(restored != mi && (!saved || restored != second));
	}
	CHECK(aesKeysClear());
	CHECK(aesKeysList(ids, &active) && !active && !ids[0]);
	const unsigned rotation_cuts[] = { 1, 2, 4, 8, 100, 252, 253, 255, 256, 257, 258 };
	for (unsigned c = 0; c < sizeof(rotation_cuts) / sizeof(rotation_cuts[0]); c++) {
		sim_flash_free();
		sim_flash_create(0x40000, &map);
		CHECK(storage_init() && aesKeysPut(1, 7, key, 1));
		for (unsigned n = 0; n < 31; n++)
			CHECK(aesKeysSelect(1, got, &id, &mi));
		sim_flash_cut_after(rotation_cuts[c]);
		bool saved = aesKeysSelect(1, got, &id, &second);
		sim_flash_power_on();
		CHECK(storage_init());
		uint32_t restored;
		CHECK(aesKeysRestore(got, &id, &restored) == 1);
		CHECK(restored != mi && (!saved || restored != second));
	}
	// A damaged old record must not hide a complete bank in the other records.
	sim_flash_data[0x2E000] = 0;
	CHECK(aesKeysRestore(got, &id, &mi) == 1);
	uint32_t newest_seq = 0, newest_addr = 0;
	for (uint32_t a = 0x2E000; a < 0x30000; a += 256) {
		uint8_t *p = sim_flash_data + a;
		uint32_t seq = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
		if (p[0] == 'A' && !p[252] && seq > newest_seq) {
			newest_seq = seq;
			newest_addr = a;
		}
	}
	CHECK(newest_addr != 0);
	sim_flash_data[newest_addr + 20] ^= 1;
	CHECK(aesKeysRestore(got, &id, &mi) == -1);	// never roll allocation back to an older bank after corruption
	// Foreign data without any complete bank is never erased or overwritten, and boot must inhibit TX.
	sim_flash_free();
	sim_flash_create(0x40000, &map);
	CHECK(storage_init());
	sim_flash_data[0x2E000] = 0;
	uint32_t before = sim_flash_units();
	CHECK(!aesKeysPut(1, 7, key, 1));
	CHECK(aesKeysRestore(got, &id, &mi) == -1);
	CHECK(sim_flash_units() == before);
	sim_flash_free();
	printf("aesKeysTest: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}

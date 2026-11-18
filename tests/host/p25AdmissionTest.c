// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Real stored records exercise TX permissions independently of tolerant receive decoding.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p25/p25_admission.h"
#include "p25/p25_codeplug.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_records.h"
#include "codeplug/codeplug_kv.h"
#include "storage/storage.h"
#include "sim/sim_flash.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
void storage_lock(void) {}
void storage_unlock(void) {}
static band_t bands[] = { {136000000, 174000000, true}, {400000000, 480000000, true} };
const board_t board = { .name = "test", .bands = bands, .band_count = 2, .power_count = 9, .flash = &sim_flash };
static const hal_flash_map_t map = {
	.settings_addr = 0x11000, .settings_size = 0x8000, .journal_addr = 0x19000,
	.openfw_eeprom = 0, .openfw_flash = 0x20000, .openfw_end = 0x200000,
	.calibration_addr = 0x10000, .calibration_size = 0x200,
};
static channel_t channel;
static uint8_t conv[P25_CP_CONV_LEN];
static codeplug_tag_t tag;
static bool (*original_read)(uint32_t addr, void *buf, uint32_t len);
static unsigned read_calls, fail_read_call;

static bool read_with_fault(uint32_t addr, void *buf, uint32_t len)
{
	read_calls++;
	if (read_calls == fail_read_call) return false;
	return original_read(addr, buf, len);
}

static void fresh(void)
{
	sim_flash_create(0x200000, &map);
	CHECK(storage_init());
	codeplug_init();
	CHECK(codeplug_set_dmr_id(1234567));
	channel = (channel_t){ .name = "P25", .rx_hz = 453100000, .tx_hz = 458100000,
		.mode = RADIO_MODE_DMR, .bw = RADIO_BW_12K5, .power = CODEPLUG_POWER_DEFAULT,
		.rx_only = false, .color_code = 1, .timeslot = 1 };
	p25_conv_t pc = {0x293, 0x293, P25_SQUELCH_NORMAL};
	p25_conv_encode(&pc, conv);
	CHECK(p25_conv_save(10, &channel, &pc));
	CHECK(codeplug_tagged_channel(10, &channel));
	CHECK(codeplug_tag_read(10, &tag));
}

static uint8_t current(void)
{
	channel_t c;
	return codeplug_tagged_channel(10, &c) ? p25_codeplug_tx_permissions(10, &c) : 0;
}

static void write_conv(const uint8_t *raw, uint16_t len)
{
	CHECK(codeplug_tag_write(10, channel.name, &tag, raw, len));
}

static void test_conventional(void)
{
	fresh();
	CHECK(current() == P25_TX_VOICE);
	CHECK(!p25_codeplug_tx_permissions(10, NULL) && !p25_codeplug_tx_permissions(11, &channel));
	channel_t stale = channel;
	stale.tx_hz++;
	CHECK(!p25_codeplug_tx_permissions(10, &stale));
	stale = channel;
	channel.rx_only = true;
	CHECK(codeplug_tagged_set_channel(10, &channel) && current() == 0);
	CHECK(!p25_codeplug_tx_permissions(10, &stale));
	channel.rx_only = false;
	CHECK(codeplug_tagged_set_channel(10, &channel) && current() == P25_TX_VOICE);
	uint8_t raw[P25_CP_CONV_LEN + 1];
	memcpy(raw, conv, sizeof conv);
	raw[5] = 0;
	for (unsigned len = 0; len <= sizeof raw; len++) {
		write_conv(raw, (uint16_t)len);
		CHECK(current() == (len == P25_CP_CONV_LEN ? P25_TX_VOICE : 0));
		p25_conv_cfg_t receive;
		CHECK(p25_conv_load(10, &channel, &receive)); // receive defaults do not grant transmit
	}
	const uint16_t invalid_nacs[] = {0x0001, 0x7000, 0x9000, 0x8F7F};
	for (unsigned n = 0; n < sizeof invalid_nacs / sizeof invalid_nacs[0]; n++) {
		for (unsigned field = 1; field <= 3; field += 2) {
			memcpy(raw, conv, sizeof conv);
			raw[field] = (uint8_t)invalid_nacs[n];
			raw[field + 1] = (uint8_t)(invalid_nacs[n] >> 8);
			write_conv(raw, sizeof conv);
			CHECK(current() == 0);
		}
	}
	memcpy(raw, conv, sizeof conv);
	raw[1] = 0x7E;
	raw[2] = 0x8F; // RX any is valid, TX any is not
	write_conv(raw, sizeof conv);
	CHECK(current() == P25_TX_VOICE);
	raw[3] = 0x7E;
	raw[4] = 0x8F;
	write_conv(raw, sizeof conv);
	CHECK(current() == 0);
	tag.kind = 0x77;
	write_conv(conv, sizeof conv);
	CHECK(current() == 0);
	tag.kind = CODEPLUG_KIND_P25;
	tag.system = 2;
	write_conv(conv, sizeof conv);
	CHECK(current() == 0);
	tag.system = CODEPLUG_SYSTEM_NONE;
	for (unsigned bit = 3; bit < 8; bit++) {
		tag.flags = (uint8_t)(CODEPLUG_TAG_TX | (1u << bit));
		write_conv(conv, sizeof conv);
		CHECK(current() == 0);
	}
	tag.flags = CODEPLUG_TAG_TX;
	for (unsigned field = 0; field < P25_CP_CONV_LEN; field++) {
		memcpy(raw, conv, sizeof conv);
		raw[field] = 0x7F;
		write_conv(raw, sizeof conv);
		CHECK(current() == 0);
	}
	write_conv(conv, sizeof conv);
	uint8_t id[5] = {0, 0, 0, 0, 0};
	for (unsigned len = 0; len <= sizeof id; len++) {
		CHECK(codeplug_kv_set(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, id, (uint16_t)len));
		CHECK(current() == (len == 4 ? P25_TX_VOICE : 0));
	}
	memset(id, 0xFF, 4);
	CHECK(codeplug_kv_set(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, id, 4));
	CHECK(current() == 0);
	const uint32_t ids[] = {1, P25_CP_UNIT_MAX, P25_CP_UNIT_MAX + 1u, 0x1000000, 0xFFFFFFFF};
	for (unsigned n = 0; n < sizeof ids / sizeof ids[0]; n++) {
		for (unsigned byte = 0; byte < 4; byte++) id[byte] = (uint8_t)(ids[n] >> (byte * 8));
		CHECK(codeplug_kv_set(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, id, 4));
		CHECK(current() == (ids[n] <= P25_CP_UNIT_MAX ? P25_TX_VOICE : 0));
	}
	CHECK(codeplug_kv_remove(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25));
	CHECK(codeplug_set_dmr_id(0) && current() == 0);
	CHECK(codeplug_set_dmr_id(1234567) && current() == P25_TX_VOICE);
	channel.contact = 123;
	CHECK(codeplug_tagged_set_channel(10, &channel) && current() == 0);
	contact_t c = { .name = "Target", .type = CONTACT_GROUP, .id = 65536 };
	CHECK(codeplug_set_contact(123, &c) && current() == 0);
	c.id = 42;
	CHECK(codeplug_set_contact(123, &c) && current() == P25_TX_VOICE);
	c.type = CONTACT_PRIVATE;
	CHECK(codeplug_set_contact(123, &c) && current() == P25_TX_VOICE);
	c.id = 0xFFFFFF;
	CHECK(codeplug_set_contact(123, &c) && current() == 0);
	c.type = CONTACT_ALL_CALL;
	CHECK(codeplug_set_contact(123, &c) && current() == P25_TX_VOICE);
	CHECK(codeplug_delete_contact(123) && current() == 0);
	channel.contact = 0;
	channel.listen_list = 12;
	CHECK(codeplug_tagged_set_channel(10, &channel) && current() == 0);
	listen_list_t list = { .name = "RX groups", .count = 2, .contacts = {123, 124} };
	c.type = CONTACT_GROUP;
	c.id = 42;
	CHECK(codeplug_set_contact(124, &c));
	CHECK(codeplug_set_listen_list(12, &list) && current() == 0); // no silent skip over a missing contact
	c.type = CONTACT_PRIVATE;
	CHECK(codeplug_set_contact(123, &c) && current() == P25_TX_VOICE);
	CHECK(codeplug_delete_contact(124) && current() == 0);
	list.count = 0;
	CHECK(codeplug_set_listen_list(12, &list) && current() == 0);
	channel.listen_list = 0;
	CHECK(codeplug_tagged_set_channel(10, &channel) && current() == P25_TX_VOICE);
	CHECK(codeplug_tag_write(10, "Stale", &tag, conv, sizeof conv));
	CHECK(current() == 0);
	write_conv(conv, sizeof conv);
	sim_flash_data[CODEPLUG_KV_ADDR + CODEPLUG_KV_HDR + 5] ^= 1;
	CHECK(current() == 0);
	CHECK(sim_flash_violations() == 0);
	fresh();
	sim_flash_data[CODEPLUG_KV_ADDR + 4] = CODEPLUG_KV_VERSION + 1;
	CHECK(current() == 0);
}

static void test_read_errors(void)
{
	for (unsigned present = 0; present < 2; present++) {
		fresh();
		uint8_t id[4] = {42, 0, 0, 0};
		if (present) CHECK(codeplug_kv_set(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, id, sizeof id));
		original_read = sim_flash.read;
		sim_flash.read = read_with_fault;
		fail_read_call = read_calls = 0;
		int len;
		CHECK(codeplug_kv_lookup(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, id, sizeof id, &len));
		CHECK(len == (present ? 4 : -1));
		unsigned total = read_calls;
		for (unsigned at = 1; at <= total; at++) {
			read_calls = 0;
			fail_read_call = at;
			CHECK(!codeplug_kv_lookup(CODEPLUG_KEY_MODE_SET + CODEPLUG_KIND_P25, id, sizeof id, &len));
			CHECK(len == -1);
		}
		fail_read_call = read_calls = 0;
		CHECK(p25_codeplug_tx_permissions(10, &channel) == P25_TX_VOICE);
		total = read_calls;
		for (unsigned at = 1; at <= total; at++) {
			read_calls = 0;
			fail_read_call = at;
			CHECK(p25_codeplug_tx_permissions(10, &channel) == 0);
		}
		sim_flash.read = original_read;
	}
	int len;
	CHECK(!codeplug_kv_lookup(0, NULL, 0, &len));
	CHECK(!codeplug_kv_lookup(0xFFFF, NULL, 0, &len));
	CHECK(!codeplug_kv_lookup(1, NULL, 1, &len));
	CHECK(!codeplug_kv_lookup(1, NULL, 0, NULL));
	CHECK(codeplug_kv_lookup(0x7777, NULL, 0, &len) && len == -1);
}

enum { EDIT_RX_ONLY, RENAME_RX_ONLY, DELETE_CHANNEL, REPLACE_CHANNEL, COPY_RX_ONLY };

static bool edit(unsigned action)
{
	channel_t c = channel;
	if (action == DELETE_CHANNEL) return codeplug_tagged_delete_channel(10);
	if (action == REPLACE_CHANNEL) {
		c.kind = CODEPLUG_KIND_NONE;
		c.system = CODEPLUG_SYSTEM_NONE;
		c.mode = RADIO_MODE_FM;
		strcpy(c.name, "Plain");
		return codeplug_tagged_set_channel(10, &c);
	}
	c.rx_only = true;
	if (action == EDIT_RX_ONLY) {
		p25_conv_t pc = {0x293, 0x293, P25_SQUELCH_NORMAL};
		return p25_conv_save(10, &c, &pc);
	}
	if (action == RENAME_RX_ONLY) {
		strcpy(c.name, "Renamed");
		return codeplug_tagged_set_channel(10, &c);
	}
	strcpy(c.name, "Copy");
	return codeplug_tagged_set_channel(11, &c) && codeplug_tag_copy(10, 11, &c);
}

static void test_power_cuts(void)
{
	uint8_t *snapshot = malloc(0x200000);
	CHECK(snapshot != NULL);
	if (snapshot == NULL) return;
	for (unsigned action = EDIT_RX_ONLY; action <= COPY_RX_ONLY; action++) {
		fresh();
		memcpy(snapshot, sim_flash_data, 0x200000);
		uint32_t start = sim_flash_units();
		CHECK(edit(action));
		uint32_t total = sim_flash_units() - start;
		CHECK(total > 0);
		unsigned old = 0, changed = 0, cuts = 0;
		// Sample journal/data writes, including both ends. This is not an exhaustive cut at every byte.
		for (uint32_t cut = 1; cut <= total + 1u; cut = cut + 37u > total + 1u ? total + 1u : cut + 37u) {
			sim_flash_power_on();
			memcpy(sim_flash_data, snapshot, 0x200000);
			CHECK(storage_init());
			codeplug_reload(); // invalidate process-local readers when restoring a simulated boot image
			sim_flash_cut_after(cut);
			bool completed = edit(action);
			if (sim_flash_is_cut()) cuts++;
			sim_flash_power_on();
			CHECK(storage_init());
			codeplug_reload();
			channel_t c, base;
			uint16_t number = action == COPY_RX_ONLY ? 11 : 10;
			bool exists = codeplug_tagged_channel(number, &c);
			uint8_t allowed = exists ? p25_codeplug_tx_permissions(number, &c) : 0;
			if (exists && c.kind == CODEPLUG_KIND_P25) {
				CHECK(codeplug_channel(number, &base) && base.mode == RADIO_MODE_DMR && base.rx_only);
				if (c.rx_only || strcmp(c.name, "Renamed") == 0 || action == COPY_RX_ONLY) {
					CHECK(c.rx_only && allowed == 0);
					changed++;
				} else {
					CHECK(strcmp(c.name, "P25") == 0 && allowed == P25_TX_VOICE);
					old++;
				}
			} else {
				CHECK(allowed == 0);
				changed++;
			}
			if (completed) CHECK(allowed == 0);
			if (action == COPY_RX_ONLY) CHECK(current() == P25_TX_VOICE);
			CHECK(sim_flash_violations() == 0);
			if (cut == total + 1u) break;
		}
		CHECK(cuts > 0 && changed > 0);
		if (action != COPY_RX_ONLY) CHECK(old > 0);
		printf("  power cuts action %u: %u cut samples over %u units; %u old, %u safe/new states\n",
		       action, cuts, total, old, changed);
	}
	free(snapshot);
}

#if CONFIG_P25_TRUNK
static void system_record(uint8_t flags, uint8_t options, uint8_t identity_len)
{
	uint8_t raw[CODEPLUG_SYS_HDR + P25_CP_TSYS_LEN] = {0};
	raw[0] = CODEPLUG_SYSTYPE_P25_TRUNK;
	raw[1] = flags;
	raw[3] = identity_len;
	memcpy(raw + 12, "System", 6);
	p25_tsys_t sys = { .wacn = 0x12345, .sysid = 0x123, .cc_nac = 0x293, .options = options };
	p25_tsys_encode(&sys, raw + CODEPLUG_SYS_HDR);
	CHECK(codeplug_kv_set(CODEPLUG_KEY_SYSTEM + 2, raw, sizeof raw));
}

static void test_trunk(void)
{
	fresh();
	tag.kind = CODEPLUG_KIND_P25_TRUNK;
	tag.system = 2;
	uint8_t body = 0;
	CHECK(codeplug_tag_write(10, channel.name, &tag, &body, 1));
	CHECK(current() == 0);
	for (unsigned rx_only = 0; rx_only < 2; rx_only++) {
		for (unsigned listen_only = 0; listen_only < 2; listen_only++) {
			for (unsigned no_reg = 0; no_reg < 2; no_reg++) {
				tag.flags = rx_only ? 0 : CODEPLUG_TAG_TX;
				CHECK(codeplug_tag_write(10, channel.name, &tag, &body, 1));
				system_record(listen_only ? 0 : CODEPLUG_SYS_TX, no_reg ? P25_TSYS_NO_REG : 0, 5);
				uint8_t expected = rx_only || listen_only ? 0 :
					P25_TX_VOICE | P25_TX_REQUEST | (no_reg ? 0 : P25_TX_REGISTRATION);
				CHECK(current() == expected);
			}
		}
	}
	tag.flags = CODEPLUG_TAG_TX;
	CHECK(codeplug_tag_write(10, channel.name, &tag, &body, 1));
	system_record(CODEPLUG_SYS_TX | CODEPLUG_SYS_NEEDS_KEY, 0, 5);
	CHECK(current() == 0);
	system_record(CODEPLUG_SYS_TX | 0x80, 0, 5);
	CHECK(current() == 0);
	system_record(CODEPLUG_SYS_TX, 0x80, 5);
	CHECK(current() == 0);
	system_record(CODEPLUG_SYS_TX, 0, 0);
	CHECK(current() == 0);
	system_record(CODEPLUG_SYS_TX, 0, 5);
	CHECK(current() == (P25_TX_VOICE | P25_TX_REQUEST | P25_TX_REGISTRATION));
	uint8_t extra[2] = {P25_TCH_FOLLOW_ALL, 0};
	for (unsigned len = 0; len <= sizeof extra; len++) {
		CHECK(codeplug_tag_write(10, channel.name, &tag, extra, (uint16_t)len));
		CHECK(current() == (len == 1 ? (P25_TX_VOICE | P25_TX_REQUEST | P25_TX_REGISTRATION) : 0));
	}
	extra[0] = 2;
	CHECK(codeplug_tag_write(10, channel.name, &tag, extra, 1));
	CHECK(current() == 0);
	CHECK(codeplug_tag_write(10, channel.name, &tag, &body, 1));
	CHECK(codeplug_tagged_channel(10, &channel));
	original_read = sim_flash.read;
	sim_flash.read = read_with_fault;
	fail_read_call = read_calls = 0;
	CHECK(p25_codeplug_tx_permissions(10, &channel) == (P25_TX_VOICE | P25_TX_REQUEST | P25_TX_REGISTRATION));
	unsigned total = read_calls;
	for (unsigned at = 1; at <= total; at++) {
		read_calls = 0;
		fail_read_call = at;
		CHECK(p25_codeplug_tx_permissions(10, &channel) == 0);
	}
	sim_flash.read = original_read;
	uint8_t record[CODEPLUG_SYS_HDR + P25_CP_TSYS_LEN + 1];
	CHECK(codeplug_kv_get(CODEPLUG_KEY_SYSTEM + 2, record, sizeof record) == (int)sizeof record - 1);
	record[sizeof record - 1] = 0;
	const unsigned fields[] = {2, 3, 7, CODEPLUG_SYS_HDR + 2, CODEPLUG_SYS_HDR + 4, CODEPLUG_SYS_HDR + 6};
	const uint8_t invalid[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x70};
	for (unsigned n = 0; n < sizeof fields / sizeof fields[0]; n++) {
		uint8_t saved = record[fields[n]];
		record[fields[n]] = invalid[n];
		CHECK(codeplug_kv_set(CODEPLUG_KEY_SYSTEM + 2, record, sizeof record - 1));
		CHECK(current() == 0);
		record[fields[n]] = saved;
	}
	for (unsigned len = 0; len <= P25_CP_TSYS_LEN + 1; len++) {
		CHECK(codeplug_kv_set(CODEPLUG_KEY_SYSTEM + 2, record, (uint16_t)(CODEPLUG_SYS_HDR + len)));
		CHECK(current() == (len == P25_CP_TSYS_LEN ? (P25_TX_VOICE | P25_TX_REQUEST | P25_TX_REGISTRATION) : 0));
	}
	system_record(CODEPLUG_SYS_TX, 0, 5);
	channel.contact = 123;
	CHECK(codeplug_tagged_set_channel(10, &channel));
	contact_t c = { .name = "Private", .type = CONTACT_PRIVATE, .id = 42 };
	CHECK(codeplug_set_contact(123, &c) && current() == 0);
	CHECK(codeplug_kv_remove(CODEPLUG_KEY_SYSTEM + 2) && current() == 0);
}
#endif

int main(void)
{
	test_conventional();
	test_read_errors();
	test_power_cuts();
#if CONFIG_P25_TRUNK
	test_trunk();
#endif
	sim_flash_free();
	printf("p25AdmissionTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

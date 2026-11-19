// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Real storage, radio state machine, follower, framing and voice; only hardware providers are simulated.
#define P25_TRUNK_STATE_TEST 1
#include "radioP25StateTest.c"
#include "codeplug/codeplug_records.h"
#include "p25/p25_favorites.h"

static void record(uint16_t key, const void *value, size_t len)
{ CHECK(codeplug_kv_set(key, value, (uint16_t)len)); }
static void system_config(bool listen, bool no_reg)
{
	uint8_t raw[CODEPLUG_SYS_HDR + P25_CP_TSYS_LEN] = {0};
	raw[0] = CODEPLUG_SYSTYPE_P25_TRUNK; raw[1] = listen ? 0 : CODEPLUG_SYS_TX; raw[3] = 5;
	memcpy(raw + 12, "System", 6);
	p25_tsys_t cfg = { .wacn = 0x12345, .sysid = 0x123, .cc_nac = 0x293,
		.options = P25_TSYS_SKIP_ENC | (no_reg ? P25_TSYS_NO_REG : 0) };
	p25_tsys_encode(&cfg, raw + CODEPLUG_SYS_HDR);
	record(CODEPLUG_KEY_SYSTEM + 2, raw, sizeof raw);
}
static void plan_row(uint8_t id, uint32_t base, bool tdma)
{
	uint8_t raw[4 + P25_CP_IDEN_LEN] = { 2, CODEPLUG_ENTRY_PLAN, id, 0 };
	p25_iden_t plan = { base, 12500, 5000000, 12500, tdma ? P25_IDEN_TDMA : P25_IDEN_FDMA, tdma ? 2 : 1 };
	p25_iden_encode(&plan, raw + 4);
	record((uint16_t)(CODEPLUG_KEY_SYS_CHAN + 2 + id), raw, sizeof raw);
}
static void trunk_setup(bool rx_only, bool listen, bool no_reg)
{
	setup(rx_only);
	CHECK(radio_p25_end());
	system_config(listen, no_reg);
	for (unsigned i = 0; i < 2; i++) {
		uint8_t raw[14] = { 2, CODEPLUG_ENTRY_CHANNEL, 0, CODEPLUG_ENTRY_CONTROL };
		uint32_t hz = 453100000u + i * 100000u, tx = hz + 5000000u;
		for (unsigned k = 0; k < 4; k++) { raw[6 + k] = (uint8_t)(hz >> (k * 8)); raw[10 + k] = (uint8_t)(tx >> (k * 8)); }
		record((uint16_t)(CODEPLUG_KEY_SYS_CHAN + i), raw, sizeof raw);
	}
	plan_row(0, 453100000, false);
	plan_row(1, 1100000000, false);
	plan_row(2, 453100000, true);
	codeplug_tag_t tag = { .kind = CODEPLUG_KIND_P25_TRUNK, .system = 2, .flags = rx_only ? 0 : CODEPLUG_TAG_TX };
	uint8_t options = 0;
	CHECK(codeplug_tag_write(10, channel.name, &tag, &options, 1));
	CHECK(codeplug_tagged_channel(10, &channel));
	request(RREQ_RELOAD, 0); step(30);
	CHECK(sm.view.ch.kind == CODEPLUG_KIND_P25_TRUNK && sm.view.p25_active && radio_p25_trunked() && loaned);
	CHECK(last_rx_hz == 453100000u && !sm.dmr_running);
	key_calls = starts = 0;
}
static p25_follow_view_t follow_view(void)
{ p25_follow_view_t v; radio_p25_trunk_view(&v); return v; }
static void signaling(const p25_tsbk_t *message)
{
	uint8_t block[P25_TSBK_BYTES], unit[P25_UNIT_BYTES_MAX];
	p25_tsbk_t one = *message;
	one.last = true;
	p25_tsbk_make(&one, block);
	uint16_t n = p25_isp_unit(unit, 0x293, block);
	deliver(unit, 0, n);
	step(10);
}
static void network(void)
{
	p25_tsbk_t m = { .kind = P25_TSBK_NET, .u.net = { .wacn = 0x12345, .sysid = 0x123, .ch = 1 } };
	signaling(&m); signaling(&m);
}
static void grant(uint32_t source, uint16_t ch, uint8_t svc)
{
	p25_tsbk_t m = { .kind = P25_TSBK_GRANT, .opcode = P25_OP_GRP_V_GRANT,
		.u.grant = { .has_svc = true, .svc = svc, .ch = ch, .tg = 1, .src = source } };
	signaling(&m); step(10);
}
static void drain(void)
{
	for (unsigned n = 0; n < 100 && on; n++) {
		pcm();
		CHECK(queued != 0);
		if (!queued) break;
		air_tx_chunk_t c = queue[0];
		memmove(queue, queue + 1, (--queued) * sizeof queue[0]);
		if (c.flags & AIR_TX_LAST) { on = false; event(AIR_EV_TX_DONE, NULL); }
		else event(AIR_EV_TX_SPACE, NULL);
		step(10);
	}
}

static uint8_t interrupt_unit[P25_UNIT_BYTES_MAX];
static uint16_t interrupt_symbols;
static void provider_after_clock_read(void)
{
	// A decoder task preempts after the clock value was sampled, before the caller resumes.
	now_us += 1000;
	for (uint16_t at = 0; at < interrupt_symbols;) {
		uint8_t packed[144 / 4] = {0};
		uint16_t take = interrupt_symbols - at > 144 ? 144 : (uint16_t)(interrupt_symbols - at);
		for (uint16_t i = 0; i < take; i++) air_sym_put(packed, i, air_sym_get(interrupt_unit, at + i));
		air_chunk_t chunk = { .t_us = now_us, .nsyms = take, .sync_at = -1, .hard = packed };
		event(AIR_EV_RX, &chunk);
		now_us += (uint32_t)take * 208u;
		at += take;
	}
}

static void test_follower_clock(void)
{
	trunk_setup(true, false, false);
	network(); step(100);
	p25_follow_view_t before = follow_view();
	uint8_t block[P25_TSBK_BYTES];
	p25_tsbk_t net = { .kind = P25_TSBK_NET, .last = true,
		.u.net = { .wacn = 0x12345, .sysid = 0x123, .ch = 1 } };
	p25_tsbk_make(&net, block);
	interrupt_symbols = p25_isp_unit(interrupt_unit, 0x293, block);
	clock_read_hook = provider_after_clock_read;
	CHECK(radio_p25_poll(NULL));
	CHECK(clock_read_hook == NULL);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL && follow_view().cc_lost == before.cc_lost &&
	      follow_view().hunts == before.hunts);
	network();
	CHECK(follow_view().blocks > before.blocks);

	grant(42, 4, 0); step(50);
	CHECK(follow_view().state == P25_FOLLOW_VOICE);
	p25_call_tx_t call;
	p25_lc_t lc = { .lcf = P25_LCF_GROUP, .tgid = 1, .src = 42 };
	p25_call_tx_begin(&call, 0x293, &lc, P25_SS_UNKNOWN);
	uint16_t first;
	interrupt_symbols = p25_call_tx_ready(&call, &first);
	// The first unit starts at symbol zero; the real framer and owner callbacks run in the injected provider.
	CHECK(first == 0);
	memcpy(interrupt_unit, call.unit, sizeof interrupt_unit);
	before = follow_view();
	clock_read_hook = provider_after_clock_read;
	CHECK(radio_p25_poll(NULL));
	CHECK(follow_view().state == P25_FOLLOW_VOICE && follow_view().voice_lost == before.voice_lost);
	CHECK(key_calls == 0 && starts == 0 && !keyed);
	CHECK(radio_p25_end());

	// A retune's elapsed time must not consume the new channel's receive window.
	trunk_setup(true, false, false);
	network();
	rx_delay_us = 1500000;
	grant(42, 4, 0);
	CHECK(follow_view().state == P25_FOLLOW_VOICE && follow_view().voice_lost == 0);
	step(200);
	CHECK(follow_view().state == P25_FOLLOW_VOICE && follow_view().voice_lost == 0);
	rx_delay_us = 1500000;
	step(P25_FOLLOW_VOICE_LOST_MS + 20); step(20);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL && follow_view().voice_lost == 1 && follow_view().cc_lost == 0);
	step(200);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL && follow_view().cc_lost == 0);
	step(P25_FOLLOW_CC_LOST_MS + 20);
	CHECK(follow_view().state == P25_FOLLOW_HUNT && follow_view().cc_lost == 1);
	CHECK(key_calls == 0 && starts == 0 && !keyed);
	CHECK(radio_p25_end());
}
int main(void)
{
	test_follower_clock();
	// The shared DMR assignment bank feeds P25 receive groups, without changing TX permissions or target.
	trunk_setup(true, false, false);
	CHECK(radio_p25_end());
	uint8_t *bank = sim_flash_data + map.openfw_flash + 0x14000;
	memset(bank, 0, 0x4000);
	memcpy(bank, "RFTP\1\0\0\0", 8); bank[8] = 1;
	for (unsigned n = 10; n <= CODEPLUG_CHANNELS_MAX; n++) bank[16 + 2 * n] = 63;
	for (unsigned n = 0; n < 63; n++) {
		contact_t group = { .id = n + 2, .type = CONTACT_GROUP };
		snprintf(group.name, sizeof group.name, "Group %u", n + 2);
		CHECK(codeplug_set_contact((uint16_t)(n + 1), &group));
		bank[16 + 2 * (CODEPLUG_CHANNELS_MAX + 1) + n * 2] = (uint8_t)(n + 1);
	}
	uint32_t crc = storage_crc32(storage_crc32(0, bank + 8, 4), bank + 16, 0x4000 - 16);
	for (unsigned n = 0; n < 4; n++) bank[12 + n] = (uint8_t)(crc >> (8 * n));
	CHECK(codeplug_reload() == CODEPLUG_OPENFW);
	request(RREQ_RELOAD, 0); step(30);
	radio_p25_mem_t *assigned_owner = (radio_p25_mem_t *)pool;
	CHECK(assigned_owner->group_count == 63 && assigned_owner->groups[62] == 64);
	CHECK(assigned_owner->trunk.cfg.ngroups == 63 && assigned_owner->trunk.cfg.talk_group == 2);
	network(); step(100);
	p25_tsbk_t assigned_grant = { .kind = P25_TSBK_GRANT, .opcode = P25_OP_GRP_V_GRANT,
		.u.grant = { .has_svc = true, .ch = 4, .tg = 500, .src = 42 } };
	signaling(&assigned_grant); step(20);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL);
	assigned_grant.u.grant.tg = 64;
	signaling(&assigned_grant); step(20);
	CHECK(follow_view().state == P25_FOLLOW_VOICE && key_calls == 0 && starts == 0);
#if CONFIG_P25_UI
	CHECK(!radio_p25_monitor_command(P25_MON_SELECT, 500));
	CHECK(radio_p25_monitor_command(P25_MON_SELECT, 63));
	CHECK(assigned_owner->trunk.cfg.talk_group == 63 && assigned_owner->work.engine.cfg.tx_lc.tgid == 63);
	network(); step(100);
	assigned_grant.u.grant.tg = 64; signaling(&assigned_grant); step(20);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL);
	assigned_grant.u.grant.tg = 63; signaling(&assigned_grant); step(20);
	CHECK(follow_view().state == P25_FOLLOW_VOICE && key_calls == 0 && starts == 0);
	request(RREQ_PTT, 1); step(30);
	CHECK(key_calls == 0 && starts == 0 && !keyed);
	request(RREQ_PTT, 0);
#endif
	CHECK(radio_p25_end());
#if CONFIG_P25_UI
	trunk_setup(true, false, false);
	network(); step(100);
	p25_monitor_t monitor;
	CHECK(radio_p25_monitor(&monitor) && monitor.channel == sm.view.channel_number);
	CHECK((monitor.flags & (P25_MON_ACTIVE | P25_MON_TRUNK | P25_MON_RX_ONLY)) ==
	      (P25_MON_ACTIVE | P25_MON_TRUNK | P25_MON_RX_ONLY));
	CHECK(!radio_p25_monitor_command(P25_MON_HOLD, 0));
	CHECK(!radio_p25_monitor_command(P25_MON_FAVORITES, 0));
	CHECK(radio_p25_monitor_command(P25_MON_HOLD, 2));
	grant(42, 4, 0); step(20);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL);
	CHECK(radio_p25_monitor(&monitor) && monitor.held_tg == 2 && monitor.listen == P25_LISTEN_HOLD);
	CHECK(monitor.tx_target == 1);
	p25_tsbk_t selected = { .kind = P25_TSBK_GRANT, .opcode = P25_OP_GRP_V_GRANT,
		.u.grant = { .has_svc = true, .ch = 4, .tg = 2, .src = 42 } };
	signaling(&selected); step(20);
	CHECK(follow_view().state == P25_FOLLOW_VOICE && key_calls == 0);
	CHECK(radio_p25_monitor(&monitor) && monitor.recent_count == 1 && monitor.recent[0].tg == 2);
	radio_p25_mem_t *owner = (radio_p25_mem_t *)pool;
	CHECK(owner->trunk.cfg.talk_group == 1 && owner->work.engine.cfg.tx_lc.tgid == 1);
	CHECK(radio_p25_monitor_command(P25_MON_ALL, 0));
	CHECK(radio_p25_end());
	CHECK(!radio_p25_monitor(&monitor) && monitor.flags == 0 && monitor.recent_count == 0);
	CHECK(!radio_p25_monitor_command(P25_MON_ALL, 0));
	// Persistent favorites are scoped to the system identity, not a global list of numbers.
	CHECK(!p25_favorites_edit(&sm.view.ch, 0, false));
	CHECK(p25_favorites_edit(&sm.view.ch, 2, false));
	CHECK(p25_favorites_edit(&sm.view.ch, 2, false));
	CHECK(p25_favorites_edit(&sm.view.ch, 3, false));
	CHECK(p25_favorites_edit(&sm.view.ch, 4, false));
	CHECK(p25_favorites_edit(&sm.view.ch, 5, false));
	CHECK(!p25_favorites_edit(&sm.view.ch, 6, false));
	uint16_t favorites[P25_MONITOR_FAVORITES]; uint8_t favorites_count;
	CHECK(p25_favorites_read(&sm.view.ch, favorites, &favorites_count) && favorites_count == 4 && favorites[0] == 2);
	CHECK(p25_favorites_edit(&sm.view.ch, 3, true));
	CHECK(radio_p25_begin(&sm.view.ch, sm.view.channel_number));
	CHECK(radio_p25_poll(NULL));
	CHECK(radio_p25_monitor(&monitor) && monitor.favorite_count == 3 && monitor.favorites[0] == 2);
	CHECK(radio_p25_monitor_command(P25_MON_FAVORITES, 0));
	CHECK(radio_p25_monitor_command(P25_MON_PROGRAMMED, 0));
	CHECK(key_calls == 0 && starts == 0);
	CHECK(radio_p25_end());
	uint8_t future_favorites[17] = {2};
	CHECK(codeplug_kv_set(CODEPLUG_KEY_P25_FAVORITES + sm.view.ch.system, future_favorites, sizeof future_favorites));
	CHECK(p25_favorites_read(&sm.view.ch, favorites, &favorites_count) && favorites_count == 0);
	CHECK(!p25_favorites_edit(&sm.view.ch, 2, false));
	CHECK(codeplug_kv_remove(CODEPLUG_KEY_P25_FAVORITES + sm.view.ch.system));
	CHECK(p25_favorites_edit(&sm.view.ch, 2, false));
	// Changing the declared identity invalidates old favorites even if the system slot is reused.
	uint8_t renamed[CODEPLUG_SYS_HDR + P25_CP_TSYS_LEN];
	int renamed_len = codeplug_kv_get(CODEPLUG_KEY_SYSTEM + sm.view.ch.system, renamed, sizeof renamed);
	CHECK(renamed_len == sizeof renamed);
	renamed[12] = 'X';
	CHECK(codeplug_kv_set(CODEPLUG_KEY_SYSTEM + sm.view.ch.system, renamed, sizeof renamed));
	CHECK(p25_favorites_read(&sm.view.ch, favorites, &favorites_count) && favorites_count == 0);
#endif
	trunk_setup(true, false, false);
	network();
	CHECK(follow_view().state == P25_FOLLOW_CONTROL);
	CHECK(audio_cb == NULL);
	step(700);
	CHECK(key_calls == 0 && starts == 0 && follow_view().reg == P25_REG_OFF);
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_RX_ONLY && key_calls == 0);
	request(RREQ_PTT, 0);
	grant(42, 4, 0);
	CHECK(follow_view().state == P25_FOLLOW_VOICE && last_rx_hz == 453150000u && key_calls == 0);
	p25_call_tx_t call;
	p25_lc_t lc = { .lcf = P25_LCF_GROUP, .tgid = 1, .src = 42 };
	p25_call_tx_begin(&call, 0x293, &lc, P25_SS_UNKNOWN);
	uint16_t first, count = p25_call_tx_ready(&call, &first);
	deliver(call.unit, first, count); step(10);
	CHECK(sm.view.state == RS_RX && sm.view.p25_rx && audio_cb != NULL);
	step(1100); step(20);
	CHECK(last_rx_hz == 453100000u && follow_view().state == P25_FOLLOW_CONTROL);
	CHECK(radio_p25_end());

	trunk_setup(false, true, false);
	network(); step(700);
	CHECK(follow_view().reg == P25_REG_OFF && key_calls == 0);
	request(RREQ_PTT, 1); step(20);
	CHECK(key_calls == 0 && sm.view.state == RS_TX_DENIED);
	request(RREQ_PTT, 0); CHECK(radio_p25_end());

	trunk_setup(false, false, true);
	network(); step(200);
	CHECK(key_calls == 0 && follow_view().reg == P25_REG_OFF);
	request(RREQ_PTT, 1); step(350);
	CHECK(keyed && on && sm.view.tx_auto && last_tx_hz == 458100000u && follow_view().req == P25_REQ_WAIT);
	drain();
	CHECK(!keyed && !on && sm.view.state == RS_IDLE && last_rx_hz == 453100000u);
	grant(42, 4, 0);
	CHECK(!keyed && follow_view().req == P25_REQ_WAIT);
	grant(1234567, 4, 0);
	CHECK(keyed && on && !sm.view.tx_auto && last_tx_hz == 458150000u && sm.view.state == RS_TX);
	request(RREQ_PTT, 0); drain(); step(20);
	CHECK(!keyed && sm.view.state == RS_IDLE && last_rx_hz == 453100000u);
	CHECK(radio_p25_end());

	trunk_setup(false, false, false);
	network();
	for (unsigned n = 0; n < 8 && key_calls == 0; n++) { step(400); network(); }
	CHECK(key_calls != 0 && sm.view.tx_auto && last_tx_hz == 458100000u);
	drain();
	p25_tsbk_t response = { .kind = P25_TSBK_REG_RSP,
		.u.reg = { .rv = P25_RV_ACCEPT, .sysid = 0x123, .id = 1234567, .addr = 1234567 } };
	signaling(&response); step(250);
	CHECK(keyed && follow_view().reg == P25_REG_AFF && sm.view.tx_auto);
	drain();
	response = (p25_tsbk_t){ .kind = P25_TSBK_AFF_RSP,
		.u.aff = { .gav = P25_RV_ACCEPT, .tg = 1, .dst = 1234567 } };
	signaling(&response);
	CHECK(follow_view().reg == P25_REG_READY && !keyed);
	channel.rx_only = true;
	CHECK(codeplug_tagged_set_channel(10, &channel)); step(30);
	CHECK(!keyed && !on && sm.view.ch.rx_only);
	unsigned before = key_calls;
	network(); step(700);
	CHECK(key_calls == before && follow_view().reg == P25_REG_OFF);
	CHECK(radio_p25_end());

	// A released request, unsupported grant, or failed start must never become a voice transmission.
	for (unsigned scenario = 0; scenario < 7; scenario++) {
		trunk_setup(false, false, true);
		network(); request(RREQ_PTT, 1); step(350); drain();
		CHECK(key_calls == 1 && !keyed);
		if (scenario == 0) request(RREQ_PTT, 0);
		if (scenario == 4) go_fail = true;
		if (scenario == 5) sm.set.tx_inhibit = true;
		uint16_t ch = scenario == 1 ? 0x1004 : scenario == 2 ? 0x2004 : scenario == 3 ? 0x3004 : 4;
		grant(1234567, ch, 0);
		if (scenario < 6) {
			CHECK(!keyed && !on && queued == 0);
			CHECK(key_calls == (scenario == 4 ? 2u : 1u));
		} else {
			CHECK(keyed && !sm.view.tx_auto);
			system_config(true, true); step(10);
			CHECK(!keyed && !on && !loaned && !opened);
		}
		request(RREQ_PTT, 0); go_fail = false;
		CHECK(radio_p25_end());
	}

	trunk_setup(true, false, false);
	step(650);
	CHECK(last_rx_hz == 453200000u);
	network();
	CHECK(follow_view().state == P25_FOLLOW_CONTROL);
	grant(42, 4, P25_SVC_PROTECTED);
	CHECK(follow_view().state == P25_FOLLOW_CONTROL && key_calls == 0);
	grant(42, 0x2004, 0);
	CHECK(follow_view().refused == P25_REFUSE_TDMA && key_calls == 0);
	for (unsigned n = 0; n < 12; n++) { network(); step(400); }
	CHECK(key_calls == 0 && starts == 0);
	request(RREQ_CH_STEP, 1);
	CHECK(sm.view.ch.mode == RADIO_MODE_FM && !loaned && !opened && audio_cb == NULL);
	request(RREQ_CH_STEP, 1);
	CHECK(sm.view.ch.mode == RADIO_MODE_DMR && sm.dmr_running && !loaned);
	request(RREQ_CH_STEP, -1); request(RREQ_CH_STEP, -1); step(20);
	CHECK(radio_p25_trunked() && sm.view.p25_active && loaned && !sm.dmr_running);
	CHECK(radio_p25_end());

	trunk_setup(false, false, false);
	sm.set.tx_inhibit = true;
	for (unsigned n = 0; n < 12; n++) { network(); step(400); }
	CHECK(key_calls == 0 && starts == 0);
	CHECK(radio_p25_end());

	trunk_setup(false, false, false);
	for (unsigned n = 0; n < 12 && !keyed; n++) { network(); step(400); }
	CHECK(keyed && sm.view.tx_auto);
	request(RREQ_PTT, 1);
	CHECK(sm.view.state == RS_TX_DENIED && !keyed && !on && queued == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	trunk_setup(false, false, true);
	network(); request(RREQ_PTT, 1); step(350); drain(); grant(1234567, 4, 0);
	CHECK(keyed && !sm.view.tx_auto);
	CHECK(!radio_programming_acquire()); step(10);
	CHECK(radio_programming_acquire() && sm.view.programming && !keyed && !on && !loaned);
	system_config(true, true);
	radio_programming_release(); step(10);
	CHECK(!sm.view.programming && sm.view.p25_active && !keyed);
	unsigned after = key_calls;
	for (unsigned n = 0; n < 12; n++) { network(); step(400); }
	CHECK(key_calls == after && follow_view().reg == P25_REG_OFF);
	request(RREQ_PTT, 1); step(20);
	CHECK(sm.view.state == RS_TX_DENIED && key_calls == after);
	request(RREQ_PTT, 0); CHECK(radio_p25_end());
	trunk_setup(false, false, false);
	// One extended-band control makes the system passive, even when an alternate or TX frequency is ordinary UHF.
	uint8_t extended[14] = {2, CODEPLUG_ENTRY_CHANNEL, 0, CODEPLUG_ENTRY_CONTROL};
	uint32_t extended_rx = 850000000, ordinary_tx = 458100000;
	for (unsigned k = 0; k < 4; k++) {
		extended[6 + k] = (uint8_t)(extended_rx >> (k * 8));
		extended[10 + k] = (uint8_t)(ordinary_tx >> (k * 8));
	}
	record(CODEPLUG_KEY_SYS_CHAN, extended, sizeof extended);
	request(RREQ_RELOAD, 0); step(30);
	CHECK(sm.view.p25_active && last_rx_hz == extended_rx);
	unsigned passive_keys = key_calls;
	for (unsigned n = 0; n < 8; n++) { network(); step(400); }
	CHECK(key_calls == passive_keys && follow_view().reg == P25_REG_OFF && !keyed);
	request(RREQ_PTT, 1); step(100);
	CHECK(key_calls == passive_keys && !keyed && !on);
	request(RREQ_PTT, 0); CHECK(radio_p25_end());
	printf("radioP25TrunkStateTest: %u checks, %u failed; owner %zu bytes\n", checks, failures, sizeof(radio_p25_mem_t));
	sim_flash_free();
	return failures != 0;
}

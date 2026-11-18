// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Real storage, radio state machine, follower, framing and voice; only hardware providers are simulated.
#define P25_TRUNK_STATE_TEST 1
#include "radioP25StateTest.c"
#include "codeplug/codeplug_records.h"

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
	plan_row(1, 900000000, false);
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
int main(void)
{
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

	printf("radioP25TrunkStateTest: %u checks, %u failed; owner %zu bytes\n", checks, failures, sizeof(radio_p25_mem_t));
	sim_flash_free();
	return failures != 0;
}

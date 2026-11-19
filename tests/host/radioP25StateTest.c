// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#define P25_STATE_TEST 1
#define main owner_component_main
#include "radioP25OwnerTest.c"
#undef main
#include "radio/radio_sm.h"
#include "sim/radio_task.h"
#include "settings/settings.h"
#include "radio/radio_programming.h"

static void (*clock_read_hook)(void);
uint32_t hal_millis(void)
{
	uint32_t now = now_us / 1000u;
	if (clock_read_hook) {
		void (*hook)(void) = clock_read_hook;
		clock_read_hook = NULL;
		hook();
	}
	return now;
}
void storage_quick_lock(void) {}
void storage_quick_unlock(void) {}
static radio_sm_t sm;
static void request(uint8_t kind, int32_t arg)
{
	radio_req_t r = { .type = kind, .arg = arg };
	radio_sm_request(&sm, &r, hal_millis());
}
static void step(unsigned ms)
{
	for (unsigned n = 0; n < ms; n += 10) {
		now_us += 10000;
		radio_sm_tick(&sm, hal_millis());
		radio_sm_poll(&sm, hal_millis());
	}
}
static void setup(bool rx_only)
{
	fresh(rx_only);
	sim_reset();
	settings_init();
	settings_t *prefs = settings_begin();
	prefs->vfo = 0; prefs->channel = 10; prefs->zone_index = 0; prefs->zone_pos = 0;
	settings_end(1);
	now_us = 1000;
	channel_t c = channel;
	c.kind = CODEPLUG_KIND_NONE; c.system = CODEPLUG_SYSTEM_NONE;
	c.mode = RADIO_MODE_FM; strcpy(c.name, "FM");
	CHECK(codeplug_set_channel(11, &c));
	c.mode = RADIO_MODE_DMR; strcpy(c.name, "DMR");
	CHECK(codeplug_set_channel(12, &c));
	radio_sm_init(&sm, 1);
	key_calls = 0;
}

#ifndef P25_TRUNK_STATE_TEST
int main(void)
{
	setup(true);
	CHECK(sm.view.channel_number == 10 && sm.view.ch.mode == RADIO_MODE_P25 && sm.view.p25_active);
	CHECK(!sm.dmr_running && physical_mode == RADIO_MODE_DMR && loaned);
	p25_call_tx_t incoming;
	p25_lc_t incoming_lc = { .lcf = P25_LCF_GROUP, .tgid = 1, .src = 42 };
	p25_call_tx_begin(&incoming, 0x293, &incoming_lc, P25_SS_UNKNOWN);
	uint16_t first, count = p25_call_tx_ready(&incoming, &first);
	deliver(incoming.unit, first, count);
	step(10);
	CHECK(sm.view.state == RS_RX && sm.view.p25_rx && (sm.view.leds & RADIO_LED_GREEN));
	request(RREQ_PTT, 1);
	CHECK(sm.view.state == RS_TX_DENIED && sm.view.tx_deny == TXDENY_RX_ONLY && key_calls == 0 && starts == 0);
	request(RREQ_PTT, 0);
	request(RREQ_CH_STEP, 1);
	CHECK(sm.view.ch.mode == RADIO_MODE_FM && !loaned && !opened && audio_cb == NULL);
	request(RREQ_CH_STEP, 1);
	CHECK(sm.view.ch.mode == RADIO_MODE_DMR && sm.dmr_running && !loaned);
	request(RREQ_CH_STEP, -1); request(RREQ_CH_STEP, -1);
	CHECK(sm.view.ch.mode == RADIO_MODE_P25 && sm.view.p25_active && !sm.dmr_running && loaned);
	CHECK(radio_p25_end());

	setup(false);
	go_fail = true;
	request(RREQ_PTT, 1);
	CHECK(sm.view.state == RS_TX_DENIED && sm.view.tx_deny == TXDENY_REJECTED);
	CHECK(!keyed && !on && queued == 0 && key_calls == 1);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	setup(false);
	request(RREQ_PTT, 1);
	request(RREQ_POWER_OFF, 0);
	CHECK(sm.power_off_req && !sm.power_off_busy && !sm.view.p25_active && !keyed && !loaned);
	request(RREQ_POWER_OFF, 1);
	step(30);
	CHECK(!sm.power_off_req && sm.view.p25_active && loaned && !keyed);
	CHECK(radio_p25_end());

	setup(false);
	return_fail = true;
	request(RREQ_CH_STEP, 1);
	CHECK(sm.view.ch.mode == RADIO_MODE_NONE && !sm.dmr_running && loaned && !opened && audio_cb == NULL);
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_MODE && key_calls == 0);
	request(RREQ_PTT, 0);
	return_fail = false;
	request(RREQ_CH_STEP, 1);
	CHECK(sm.view.ch.mode == RADIO_MODE_DMR && sm.dmr_running && !loaned);
	request(RREQ_CH_STEP, -1);
	CHECK(radio_p25_end());

	setup(false);
	request(RREQ_PTT, 2);
	CHECK(sm.view.tx_deny == TXDENY_MODE && key_calls == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	setup(false);
	sm.set.tx_inhibit = true;
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_INHIBIT && key_calls == 0);
	request(RREQ_PTT, 0); sm.set.tx_inhibit = false;
	sm.batt.level = BATT_CRITICAL;
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_BATTERY && key_calls == 0);
	request(RREQ_PTT, 0); sm.batt.level = BATT_OK;
	sm.set.bcl = RADIO_BCL_CARRIER; sm.view.p25_rx = true;
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_BUSY && key_calls == 0);
	request(RREQ_PTT, 0); sm.set.bcl = RADIO_BCL_OFF; sm.view.p25_rx = false;
	sm.view.tot_s = 1;
	request(RREQ_PTT, 1);
	CHECK(keyed && on);
	step(1010);
	CHECK(!keyed && !on && sm.view.state == RS_TX_DENIED && sm.view.tx_deny == TXDENY_TIMEOUT);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	setup(false);
	request(RREQ_PTT, 1); request(RREQ_PTT, 0);
	CHECK(sm.view.state == RS_TX && keyed);
	for (unsigned n = 0; n < 100 && sm.view.state == RS_TX; n++) {
		pcm();
		CHECK(queued > 0);
		if (!queued) break;
		air_tx_chunk_t c = queue[0];
		memmove(queue, queue + 1, (--queued) * sizeof queue[0]);
		if (c.flags & AIR_TX_LAST) { on = false; event(AIR_EV_TX_DONE, NULL); }
		else event(AIR_EV_TX_SPACE, NULL);
		step(30);
	}
	CHECK(sm.view.state == RS_IDLE && !keyed && !on && loaned);
	request(RREQ_CH_STEP, 1);
	CHECK(!loaned && sm.view.ch.mode == RADIO_MODE_FM);

	setup(false);
	channel_t base;
	CHECK(codeplug_channel(10, &base)); base.rx_only = false;
	CHECK(codeplug_set_channel(10, &base));
	step(30);
	CHECK(sm.view.ch.mode == RADIO_MODE_NONE && !loaned);
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_MODE && key_calls == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	setup(false);
	codeplug_tag_t unknown = { .kind = CODEPLUG_KIND_NXDN, .system = CODEPLUG_SYSTEM_NONE, .flags = CODEPLUG_TAG_TX };
	CHECK(codeplug_tag_write(10, channel.name, &unknown, NULL, 0));
	step(30);
	CHECK(sm.view.ch.mode == RADIO_MODE_NONE && !loaned && !sm.dmr_running);
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_MODE && key_calls == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());
	setup(false);
	unknown.kind = CODEPLUG_KIND_P25_TRUNK;
	CHECK(codeplug_tag_write(10, channel.name, &unknown, NULL, 0));
	step(30);
	CHECK(sm.view.ch.mode == RADIO_MODE_NONE && !loaned && !sm.dmr_running);
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_MODE && key_calls == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	setup(false);
	request(RREQ_PTT, 1);
	CHECK(sm.view.state == RS_TX && keyed && on && key_calls == 1 && starts == 1);
	CHECK(!radio_programming_acquire());
	step(10);
	CHECK(radio_programming_acquire() && sm.view.programming && !keyed && !opened && !loaned && audio_cb == NULL);
	request(RREQ_PTT, 1);
	CHECK(!keyed && !sm.ptt);
	channel.rx_only = true;
	CHECK(codeplug_tagged_set_channel(10, &channel));
	radio_programming_release();
	step(10);
	CHECK(!sm.view.programming && sm.view.ch.rx_only && sm.view.p25_active);
	key_calls = 0;
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_RX_ONLY && !keyed && key_calls == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	setup(false);
	request(RREQ_PTT, 1);
	channel.rx_only = true;
	CHECK(codeplug_tagged_set_channel(10, &channel));
	step(10);
	CHECK(!keyed && !on && !opened && !loaned && sm.view.state == RS_TX_DENIED);
	request(RREQ_PTT, 0); step(30);
	CHECK(sm.view.ch.rx_only && sm.view.p25_active);
	key_calls = 0;
	request(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_RX_ONLY && key_calls == 0);
	request(RREQ_PTT, 0);
	CHECK(radio_p25_end());

	printf("radioP25StateTest: %u checks, %u failed\n", checks, failures);
	sim_flash_free();
	return failures != 0;
}
#endif

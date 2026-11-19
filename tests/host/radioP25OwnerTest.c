// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Real stored records, engine and voice path; only air, PCM, RF and codec-loan hardware are simulated.
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "radio/radio_p25.h"
#include "p25/p25_codeplug.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_kv.h"
#include "storage/storage.h"
#include "air/air_syms.h"
#include "sim/sim_flash.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
void storage_lock(void) {}
void storage_unlock(void) {}
static bool ambe_running;
#ifndef P25_STATE_TEST
bool ambeVoiceStreaming(void) { return ambe_running; }
#else
#include "sim/dmr_sim_modem.h"
#endif
static unsigned sequence, close_at, off_at, stop_at, return_at;
static bool opened, on, keyed, audio_fail, open_fail, go_fail, return_fail, loaned, edit_on_open;
static uint32_t fit, now_us, token;
#if (CONFIG_P25_TRUNK || CONFIG_P25_UI) && !defined(P25_STATE_TEST)
uint32_t hal_millis(void) { return now_us / 1000u; }
#endif
static unsigned queued, starts, lends;
static air_tx_chunk_t queue[4];
static air_event_cb_t air_cb;
static void *air_ctx;
static audio_block_cb_t audio_cb;
static void *audio_ctx;
static uint64_t pool[(sizeof(radio_p25_mem_t) + 7u) / 8u];
static size_t capacity;

static void event(uint8_t kind, const air_chunk_t *chunk)
{
	air_event_t e = { .kind = kind, .t_us = now_us, .chunk = chunk };
	if (air_cb) air_cb(&e, air_ctx);
}
static uint32_t air_fit(const air_profile_t *p) { return p->sym_rate == 4800 ? fit : 0; }
static bool air_open(const air_profile_t *p, air_event_cb_t cb, void *ctx)
{
	(void)p;
	if (open_fail) return false;
	opened = true; air_cb = cb; air_ctx = ctx;
	if (edit_on_open) CHECK(codeplug_kv_set_u32(0x7000, 42));
	return true;
}
static void air_close(void) { opened = on = false; queued = 0; air_cb = NULL; close_at = ++sequence; }
static void restart(void) {}
static bool air_go(void) { starts++; if (go_fail) return false; on = true; return true; }
static bool air_queue(const air_tx_chunk_t *c) { if (queued == 4) return false; queue[queued++] = *c; return true; }
static uint8_t air_space(void) { return (uint8_t)(4u - queued); }
static void air_end(void) { if (queued) queue[queued - 1].flags |= AIR_TX_LAST; }
static void air_abort(void) { queued = 0; on = false; event(AIR_EV_TX_DONE, NULL); }
static uint32_t air_now(void) { return now_us; }
static const hal_air_t air = {
	.fit = air_fit, .open = air_open, .close = air_close, .rx_restart = restart,
	.tx_begin = air_go, .tx_queue = air_queue, .tx_space = air_space, .tx_end = air_end,
	.tx_abort = air_abort, .now_us = air_now, .tx_chunk_syms = 144, .tx_queue_depth = 4, .tx_lead_ms = 60,
};
static bool audio_start(audio_block_cb_t cb, void *ctx)
{
	if (audio_fail || audio_cb != NULL) return false;
	audio_cb = cb; audio_ctx = ctx;
	return true;
}
static void audio_stop(void) { audio_cb = NULL; stop_at = ++sequence; }
static void speaker(bool on_pcm) { (void)on_pcm; }
static const hal_audio_t audio = { .start = audio_start, .stop = audio_stop, .speaker_pcm = speaker };
static void tx_off(void) { keyed = false; off_at = ++sequence; }
static unsigned key_calls;
static radio_mode_t physical_mode;
static uint32_t last_rx_hz, last_tx_hz;
static uint32_t rx_delay_us;
static void set_mode(radio_mode_t mode) { physical_mode = mode; }
static bool rx(uint32_t hz, radio_bw_t bw, const subtone_t *tone)
{
	(void)bw; (void)tone;
	last_rx_hz = hz;
	now_us += rx_delay_us; rx_delay_us = 0;
	return board_in_band(hz, false);
}
static bool tx_rf(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tone)
{ (void)bw; (void)power; (void)tone; last_tx_hz = hz; key_calls++; keyed = board_in_band(hz, true); return keyed; }
static void status(radio_status_t *s) { *s = (radio_status_t){ .rssi_dbm = -120, .noise = 255, .tone_ok = true }; }
static void audio_route(bool open) { (void)open; }
static const hal_radio_t rf = { .tx_off = tx_off, .set_mode = set_mode, .rx = rx, .tx = tx_rf, .status = status, .audio = audio_route };
static uint16_t battery(void) { return 8000; }
static uint8_t percent(void) { return 100; }
static int8_t volume(void) { return -1; }
static void led(uint8_t which, bool enabled) { (void)which; (void)enabled; }
static const hal_power_t power_hal = { .battery_mv = battery, .battery_pct = percent, .volume = volume, .led = led };
static bool lend(size_t bytes, size_t alignment, hal_vocoder_loan_t *out)
{
	lends++;
	if (loaned || bytes > capacity || alignment > 8) return false;
	loaned = true;
	*out = (hal_vocoder_loan_t){ .data = pool, .bytes = capacity, .token = ++token };
	return true;
}
static bool reclaim(hal_vocoder_loan_t *in)
{
	CHECK(!opened && !on && !keyed && audio_cb == NULL);
	if (return_fail || !loaned || in->data != pool || in->token != token) return false;
	loaned = false; return_at = ++sequence;
	return true;
}
static bool codec_present(void) { return !loaned; }
static void codec_reset(void) {}
static void codec_encode(const int16_t *in, uint8_t *out) { (void)in; CHECK(!loaned); memset(out, 0, VOCODER_BITS); }
static void codec_decode(const uint8_t *in, int16_t *out) { (void)in; CHECK(!loaned); memset(out, 0, AUDIO_BLOCK_SAMPLES * sizeof *out); }
static const hal_vocoder_t vocoder = { .present = codec_present, .reset = codec_reset, .encode = codec_encode,
	.decode = codec_decode, .lend = lend, .reclaim = reclaim };
static const band_t bands[] = {{136000000,174000000,true},{400000000,480000000,true},{490000000,491000000,false},
	{740000000,999999990,false}};
const board_t board = { .name = "test", .bands = bands, .band_count = sizeof(bands) / sizeof(bands[0]), .power_count = 9,
	.flash = &sim_flash, .air = &air, .audio = &audio, .radio = &rf, .vocoder = &vocoder, .power = &power_hal,
#ifdef P25_STATE_TEST
	.dmr = &sim_modem,
#endif
};
static const hal_flash_map_t map = { .settings_addr = 0x11000, .settings_size = 0x8000,
	.journal_addr = 0x19000, .openfw_eeprom = 0, .openfw_flash = 0x20000, .openfw_end = 0x200000,
	.calibration_addr = 0x10000, .calibration_size = 0x200 };
static channel_t channel;

static void fresh(bool rx_only)
{
	rx_delay_us = 0;
	return_fail = false;
	CHECK(radio_p25_end());
	CHECK(!loaned);
	opened = on = keyed = audio_fail = open_fail = go_fail = ambe_running = edit_on_open = false;
	queued = starts = lends = sequence = close_at = off_at = stop_at = return_at = 0;
	fit = AIR_FIT_RX | AIR_FIT_TX;
	capacity = sizeof pool;
	sim_flash_create(0x200000, &map);
	CHECK(storage_init());
	codeplug_init();
	CHECK(codeplug_set_dmr_id(1234567));
	channel = (channel_t){ .name = "P25", .rx_hz = 453100000, .tx_hz = 458100000, .mode = RADIO_MODE_DMR,
		.bw = RADIO_BW_12K5, .power = CODEPLUG_POWER_DEFAULT, .rx_only = rx_only, .color_code = 1, .timeslot = 1 };
	p25_conv_t pc = {0x293, 0x293, P25_SQUELCH_NORMAL};
	CHECK(p25_conv_save(10, &channel, &pc));
	CHECK(codeplug_tagged_channel(10, &channel));
	p25VoiceResetStats();
}

static void pcm(void)
{
	int16_t mic[AUDIO_BLOCK_SAMPLES] = {0}, spk[AUDIO_BLOCK_SAMPLES];
	if (audio_cb) audio_cb(mic, spk, audio_ctx);
}

static void deliver(const uint8_t *syms, uint16_t first, uint16_t n)
{
	while (n) {
		uint8_t packed[144 / 4] = {0};
		uint16_t take = n > 144 ? 144 : n;
		for (unsigned i = 0; i < take; i++) air_sym_put(packed, i, air_sym_get(syms, first + i));
		air_chunk_t c = { .t_us = now_us, .nsyms = take, .sync_at = -1, .hard = packed };
		event(AIR_EV_RX, &c);
		now_us += (uint32_t)take * 208u;
		CHECK(radio_p25_poll(NULL));
		pcm();
		first += take; n -= take;
	}
}

static void test_receive(void)
{
	fresh(true);
	CHECK(radio_p25_begin(&channel, 10) && loaned && opened);
	CHECK(!radio_p25_tx_prepare() && !radio_p25_tx_go() && queued == 0 && starts == 0);
	p25_call_tx_t tx;
	p25_lc_t lc = { .lcf = P25_LCF_GROUP, .tgid = 1, .src = 42 };
	p25_call_tx_begin(&tx, 0x293, &lc, P25_SS_UNKNOWN);
	unsigned voice = 0;
	for (unsigned guard = 0; guard < 500 && !p25_call_tx_done(&tx); guard++) {
		uint16_t first, n = p25_call_tx_ready(&tx, &first);
		if (n) { deliver(tx.unit, first, n); p25_call_tx_sent(&tx, n); }
		else if (voice < 18) { p25_call_tx_voice(&tx, imbe_silence); voice++; }
		else p25_call_tx_end(&tx, imbe_silence);
	}
	CHECK(p25_call_tx_done(&tx));
	p25_engine_view_t view;
	CHECK(radio_p25_poll(&view) && view.rx_calls == 1 && view.rx.lc.src == 42 && view.rx.lc.tgid == 1);
#if CONFIG_DIAG
	radio_p25_diag_t d;
	CHECK(radio_p25_diag(&d) && d.active && d.chunks > 0 && d.symbols > 0);
	CHECK(d.syncs > 0 && d.frames > 0 && d.tuned_hz == channel.rx_hz && d.tuned);
#endif
	p25_voice_stats_t stats;
	p25VoiceGetStats(&stats);
	CHECK(stats.rx_in == 18 && stats.rx_frames > 0 && audio_cb != NULL);
	CHECK(radio_p25_end());
	CHECK(close_at < off_at && off_at < stop_at && stop_at < return_at && !loaned);
	CHECK(!radio_p25_running() && !radio_p25_tx_prepare() && radio_p25_end());
#if CONFIG_DIAG
	CHECK(radio_p25_diag(&d) && !d.active && d.chunks == 0 && d.frames == 0);
	CHECK(!radio_p25_diag(NULL));
#endif
}

static void test_transmit(void)
{
	fresh(false);
	CHECK(radio_p25_begin(&channel, 10));
	CHECK(radio_p25_tx_prepare() && queued > 0 && audio_cb != NULL && starts == 0);
	keyed = true; // the future state-machine caller keys only after its normal TX checks
	CHECK(radio_p25_tx_go() && on && starts == 1);
	p25_call_rx_t rx;
	air_framer_t fr;
	uint8_t frame[P25_UNIT_BYTES_MAX];
	const p25_call_sink_t sink = {0};
	p25_call_rx_init(&rx, 0x293, &sink, NULL);
	air_framer_init(&fr, &p25_air_mode, &rx.rx, frame, NULL, P25_UNIT_SYMS_MAX);
	for (unsigned tick = 0; tick < 500 && radio_p25_tx_state() != AIR_TX_DONE; tick++) {
		pcm();
		if (tick == 20) radio_p25_tx_stop();
		CHECK(queued > 0);
		if (!queued) break;
		air_tx_chunk_t c = queue[0];
		memmove(queue, queue + 1, (--queued) * sizeof queue[0]);
		air_chunk_t chunk = { .t_us = now_us, .nsyms = c.nsyms, .sync_at = -1, .hard = c.syms };
		air_framer_rx(&fr, &chunk);
		now_us += 30000;
		if (c.flags & AIR_TX_LAST) { on = false; event(AIR_EV_TX_DONE, NULL); }
		else event(AIR_EV_TX_SPACE, NULL);
	}
	CHECK(radio_p25_tx_state() == AIR_TX_DONE && !on);
	CHECK(rx.stats.calls == 1 && rx.stats.voice >= 9 && rx.call.lc.src == 1234567 && rx.call.lc.tgid == 1);
	radio_p25_tx_clear();
	CHECK(!keyed && radio_p25_tx_state() == AIR_TX_IDLE);
	CHECK(radio_p25_end() && !loaned);
}

static void test_failures(void)
{
	fresh(false);
	ambe_running = true;
	CHECK(!radio_p25_begin(&channel, 10) && lends == 0);
	ambe_running = false;
	capacity = sizeof(radio_p25_mem_t) - 1;
	CHECK(!radio_p25_begin(&channel, 10) && !loaned);
	capacity = sizeof pool;
	open_fail = true;
	CHECK(!radio_p25_begin(&channel, 10) && !loaned && !audio_cb);
	open_fail = false;
	edit_on_open = true;
	CHECK(!radio_p25_begin(&channel, 10) && !loaned && !opened);
	edit_on_open = false;
	CHECK(radio_p25_begin(&channel, 10));
	CHECK(!radio_p25_begin(&channel, 10));
	audio_fail = true;
	CHECK(!radio_p25_tx_prepare() && !on && queued == 0);
	radio_p25_tx_clear();
	audio_fail = false;
	CHECK(radio_p25_tx_prepare());
	keyed = true; go_fail = true;
	CHECK(!radio_p25_tx_go() && !keyed && queued == 0 && !on);
	go_fail = false;
	radio_p25_tx_clear();
	CHECK(radio_p25_tx_prepare());
	uint8_t value = 1;
	CHECK(codeplug_kv_set(0x7000, &value, 1));
	keyed = true;
	CHECK(!radio_p25_tx_go() && starts == 1 && !keyed && queued == 0);
	CHECK(!radio_p25_poll(NULL) && !opened && !loaned);
	CHECK(radio_p25_begin(&channel, 10));
	CHECK(radio_p25_tx_prepare());
	keyed = true;
	CHECK(radio_p25_tx_go());
	channel.rx_only = true;
	CHECK(codeplug_tagged_set_channel(10, &channel));
	CHECK(!radio_p25_poll(NULL) && !keyed && !opened && audio_cb == NULL && !loaned);
	CHECK(radio_p25_begin(&channel, 10));
	return_fail = true;
	CHECK(!radio_p25_end() && loaned && !radio_p25_running());
	CHECK(!radio_p25_begin(&channel, 10));
	return_fail = false;
	CHECK(radio_p25_end() && !loaned);

	fresh(false);
	channel.rx_hz = 800000000;
	CHECK(!radio_p25_begin(&channel, 10) && lends == 0);
	fresh(false);
	channel.tx_hz = 490500000;
	CHECK(codeplug_tagged_set_channel(10, &channel));
	CHECK(radio_p25_begin(&channel, 10) && !radio_p25_tx_prepare());
	CHECK(radio_p25_end());
	fresh(false);
	fit = AIR_FIT_RX;
	CHECK(radio_p25_begin(&channel, 10) && !radio_p25_tx_prepare() && !radio_p25_tx_go() && starts == 0);
	CHECK(radio_p25_end());
	fresh(false);
	fit = 0;
	CHECK(!radio_p25_begin(&channel, 10) && lends == 0);
	fresh(false);
	channel.tx_hz++;
	CHECK(!radio_p25_begin(&channel, 10) && lends == 0);
	fresh(false);
	codeplug_tag_t tag = { .kind = CODEPLUG_KIND_P25, .system = CODEPLUG_SYSTEM_NONE, .flags = CODEPLUG_TAG_TX };
	CHECK(codeplug_tag_write(10, channel.name, &tag, NULL, 0));
	CHECK(radio_p25_begin(&channel, 10) && !radio_p25_tx_prepare()); // tolerant receive, no implicit TX defaults
	CHECK(radio_p25_end());
	fresh(false);
	CHECK(radio_p25_begin(&channel, 10));
	uint8_t before = codeplug_kv_serial();
	for (unsigned n = 0; n < 256; n++) CHECK(codeplug_kv_set_u32(0x7000, n));
	CHECK(codeplug_kv_serial() == before);
	CHECK(!radio_p25_poll(NULL) && !loaned); // session generation must not wrap with the UI's low-byte serial
}

static void test_extended_receive_only(void)
{
	fresh(false);
	channel.rx_hz = 850000000; // even a stale TX-enabled tag and an ordinary TX frequency cannot bypass the RX band
	CHECK(codeplug_tagged_set_channel(10, &channel));
	CHECK(radio_p25_begin(&channel, 10));
	CHECK(!radio_p25_tx_prepare() && !radio_p25_tx_go() && starts == 0 && !keyed);
	CHECK(radio_p25_end());
}

int main(void)
{
	// Equal numeric IDs in different protocols must not borrow each other's names.
	fresh(true);
	contact_t named = { .name = "DMR Operations", .id = 1234, .type = CONTACT_GROUP };
	CHECK(codeplug_set_contact(1, &named));
	strcpy(named.name, "P25 Operations"); CHECK(codeplug_set_contact(2, &named));
	uint16_t binding = codeplug_name_check(named.name);
	uint8_t scope[7] = {1, 2, (uint8_t)binding, (uint8_t)(binding >> 8), 0xD2, 4, 0};
	CHECK(codeplug_kv_set(CODEPLUG_KEY_CONTACT_PROTOCOL + 1, scope, sizeof scope));
	contact_t found;
	CHECK(codeplug_contact_find(1234, true, &found) && strcmp(found.name, "DMR Operations") == 0);
	CHECK(codeplug_contact_find_protocol(1234, true, 2, &found) && strcmp(found.name, "P25 Operations") == 0);
	strcpy(named.name, "Reused contact"); CHECK(codeplug_set_contact(2, &named));
	CHECK(!codeplug_contact_find_protocol(1234, true, 2, &found));
	// Legacy RX lists remain a receive fallback when no shared operating assignments exist.
	fresh(true);
	contact_t group = { .name = "Operations", .type = CONTACT_GROUP, .id = 1234 };
	CHECK(codeplug_set_contact(1, &group));
	group.id = 65535; CHECK(codeplug_set_contact(2, &group));
	group.id = 70000; CHECK(codeplug_set_contact(3, &group));
	listen_list_t groups = { .name = "P25 groups", .count = 3, .contacts = {1, 2, 3} };
	CHECK(codeplug_set_listen_list(1, &groups));
	channel.listen_list = 1;
	p25_conv_t selective = {0x293, 0x293, P25_SQUELCH_SELECTIVE};
	CHECK(p25_conv_save(10, &channel, &selective));
	CHECK(codeplug_tagged_channel(10, &channel));
	CHECK(radio_p25_begin(&channel, 10));
	radio_p25_mem_t *owner = (radio_p25_mem_t *)pool;
	CHECK(owner->group_count == 2 && owner->groups[0] == 1234 && owner->groups[1] == 65535);
	CHECK(owner->work.engine.cfg.rx_group_count == 2 && owner->work.engine.cfg.tx_lc.tgid == 1234);
#if CONFIG_P25_UI
	CHECK(radio_p25_monitor_command(P25_MON_SELECT, 65535));
	CHECK(owner->work.engine.cfg.tx_lc.tgid == 65535 && owner->work.engine.cfg.rx_tgid == 65535);
	CHECK(!radio_p25_tx_prepare() && !keyed && starts == 0);
#endif
	CHECK(radio_p25_end());
	test_receive(); test_transmit(); test_failures();
	test_extended_receive_only();
	CHECK(radio_p25_end());
	sim_flash_free();
	printf("radioP25OwnerTest: %u checks, %u failed\n", checks, failures);
	return failures != 0;
}

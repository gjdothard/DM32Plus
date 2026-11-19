// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// Conventional lifecycle and parameters adapted from RadioFoundry c34dc440; strict leases and permissions are local.
#include "config.h"
#if CONFIG_P25
#include <string.h>
#include "board.h"
#include "radio/radio_p25.h"
#include "p25/p25_admission.h"
#include "p25/p25_codeplug.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_kv.h"
#include "storage/storage.h"
#include "dmr/ambe/ambeVoice.h"
#include "radio/radio_am.h"
#include "radio/radio_bcast.h"
#if CONFIG_P25_TRUNK
#include "hal_time.h"
#endif

static p25_workspace_lease_t lease;
static radio_p25_mem_t *mem;
_Static_assert(_Alignof(radio_p25_mem_t) <= _Alignof(p25_workspace_t), "owner loan alignment");

static bool current(void)
{
	storage_lock();
	bool ok = mem != NULL && mem->base_generation == codeplug_serial() &&
		  mem->settings_generation == codeplug_kv_generation();
	storage_unlock();
	return ok;
}

static bool receive_band(uint32_t hz)
{
	if (!board_in_band(hz, false)) return false;
#if CONFIG_AM_RX
	if (radio_am_tx_refused(hz)) return false;
#endif
#if CONFIG_BCAST_FM
	if (radio_bcast_band(hz)) return false;
#endif
	return true;
}

#if CONFIG_P25_TRUNK
// The provider never waits on the radio task. A preempting provider skips follower work while the task owns it.
static bool enter(volatile uint8_t *mine, const volatile uint8_t *other)
{
	*mine = 1;
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	if (*other) { *mine = 0; return false; }
	return true;
}
static void leave(volatile uint8_t *mine) { __atomic_signal_fence(__ATOMIC_SEQ_CST); *mine = 0; }
bool radio_p25_trunked(void) { return mem != NULL && mem->channel.kind == CODEPLUG_KIND_P25_TRUNK; }
static void trunk_unit(const p25_rx_unit_t *u, void *ctx)
{
	(void)ctx;
	if (!radio_p25_trunked()) return;
	radio_p25_trunk_t *t = &mem->trunk;
	if (!t->tuned || t->want != t->done || !enter(&t->in_prov, &t->in_radio)) return;
	p25_follow_unit(&mem->work.follower, u, hal_millis());
	leave(&t->in_prov);
}
static void trunk_call(const p25_call_t *c, void *ctx)
{
	(void)ctx;
	if (!radio_p25_trunked()) return;
	radio_p25_trunk_t *t = &mem->trunk;
	if (!t->tuned || t->want != t->done || !enter(&t->in_prov, &t->in_radio)) return;
	p25_follow_call(&mem->work.follower, c, hal_millis());
	leave(&t->in_prov);
}
static bool trunk_playing(void)
{
	return radio_p25_trunked() && mem->trunk.tuned && mem->trunk.want == mem->trunk.done &&
	       mem->work.follower.view.state == P25_FOLLOW_VOICE && !mem->work.follower.tuning;
}
static void trunk_rx_start(const p25_call_t *c, void *ctx)
{ if (trunk_playing()) p25VoiceHooks()->rx_start(c, ctx); }
static void trunk_rx_update(const p25_call_t *c, void *ctx)
{ if (trunk_playing()) p25VoiceHooks()->rx_update(c, ctx); }
static void trunk_rx_voice(const uint8_t *v, uint8_t errors, void *ctx)
{ if (trunk_playing()) p25VoiceHooks()->rx_voice(v, errors, ctx); }
static void trunk_tune(uint32_t hz, uint8_t what, uint32_t id, void *ctx)
{
	radio_p25_trunk_t *t = ctx;
	t->hz = hz; t->what = what; t->id = id;
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	t->want++;
}
static bool trunk_send(uint32_t hz, const uint8_t *block, void *ctx)
{
	radio_p25_trunk_t *t = ctx;
	memcpy(t->burst, block, P25_TSBK_BYTES);
	t->burst_hz = hz;
	t->burst_tune = t->want;
	t->burst_permission = (block[0] & 0x3Fu) == P25_ISP_GRP_V_REQ ? P25_TX_REQUEST : P25_TX_REGISTRATION;
	t->burst_pending = true;
	return true;
}
static const p25_follow_hooks_t follow_hooks = { .tune = trunk_tune, .send = trunk_send };

static bool trunk_tx_band(void)
{
	if (board_receive_only(mem->channel.rx_hz)) return false;
	for (uint8_t i = 0; i < mem->trunk.cfg.ncc; i++)
		if (board_receive_only(mem->trunk.cc[i].hz)) return false;
	return true;
}

static bool trunk_params(p25_engine_cfg_t *cfg)
{
	p25_trunk_cfg_t *l = &mem->work.load;
	if (!p25_trunk_load(mem->number, &mem->channel, l) || l->ctl_count == 0) return false;
	radio_p25_trunk_t *t = &mem->trunk;
	p25_follow_cfg_t *c = &t->cfg;
	c->wacn = l->sys.wacn; c->sysid = l->sys.sysid; c->nac = l->sys.cc_nac;
	c->flags = P25_FOLLOW_SKIP_ENC;
	if (l->sys.options & P25_TSYS_PLAN_ONLY) c->flags |= P25_FOLLOW_PLAN_ONLY;
	if (l->follow_all) c->flags |= P25_FOLLOW_ALL_GROUPS;
	c->unit_id = l->unit_id; c->talk_group = l->talk_group;
	uint8_t permission = p25_codeplug_tx_permissions(mem->number, &mem->channel);
	if ((permission & (P25_TX_REQUEST | P25_TX_VOICE)) == (P25_TX_REQUEST | P25_TX_VOICE)) c->flags |= P25_FOLLOW_TX;
	if (!(permission & P25_TX_REGISTRATION)) c->flags |= P25_FOLLOW_NO_REG;
	c->ncc = l->ctl_count;
	for (uint8_t i = 0; i < c->ncc; i++)
		t->cc[i] = (p25_follow_cc_t){ l->ctl[i].rx_hz, l->ctl[i].tx_hz, l->ctl[i].chan, l->ctl_site[i] };
	c->cc = t->cc;
	if (!trunk_tx_band()) {
		c->flags &= (uint8_t)~P25_FOLLOW_TX;
		c->flags |= P25_FOLLOW_NO_REG;
	}
	c->iden_set = l->iden_set;
	for (uint8_t i = 0; i < P25_FOLLOW_IDENS; i++)
		t->iden[i] = (p25_follow_iden_t){ l->iden[i].base_hz, l->iden[i].spacing_hz, l->iden[i].tx_offset_hz,
			l->iden[i].slots, l->iden[i].type == P25_IDEN_TDMA };
	c->iden = t->iden;
	c->hold_ms = (uint32_t)l->sys.hold_s * 1000u;
	memset(cfg, 0, sizeof *cfg);
	cfg->silence = imbe_silence; cfg->trunk = true;
	cfg->rx_nac = cfg->tx_nac = c->nac;
	cfg->unit_id = c->unit_id;
	cfg->tx_lc.lcf = P25_LCF_GROUP; cfg->tx_lc.tgid = c->talk_group;
	return true;
}

static bool trunk_allowed(uint32_t hz, uint8_t permission)
{
	return current() && trunk_tx_band() && receive_band(hz) && board_in_band(hz, true) &&
	       (mem->work.engine.session.fit & AIR_FIT_TX) != 0 &&
	       (p25_codeplug_tx_permissions(mem->number, &mem->channel) & permission) == permission;
}
static void trunk_nac(void)
{
	p25_engine_cfg_t cfg = mem->work.engine.cfg;
	uint16_t nac = mem->work.follower.view.nac;
	if (mem->trunk.cfg.nac == P25_NAC_ANY && nac <= 0xFFFu && nac != P25_NAC_ANY && nac != P25_NAC_REPEAT_ANY) {
		cfg.tx_nac = nac;
		p25_engine_set(&mem->work.engine, &cfg);
	}
}
// Called only on the radio task, under storage_lock. RF admission is repeated by key(), after preparing symbols.
static void trunk_service(void)
{
	radio_p25_trunk_t *t = &mem->trunk;
	p25_follow_t *f = &mem->work.follower;
	uint32_t now = hal_millis();
	if (!enter(&t->in_radio, &t->in_prov)) return;
	if (t->tx_kind == 1 && p25_engine_tx_state(&mem->work.engine) == AIR_TX_DONE) {
		board.radio->tx_off(); p25_engine_tx_clear(&mem->work.engine);
		t->tx_kind = 0; t->tx_hz = 0;
		t->tuned = false; t->want++;
		t->hz = f->view.cc_hz; t->what = P25_FOLLOW_TUNE_CONTROL; t->id = 0;
	}
	if (t->want != t->done && t->tx_kind == 0) {
		static const subtone_t none = { TONE_NONE, 0 };
		t->done = t->want;
		t->tuned = receive_band(t->hz) && board.radio->rx(t->hz, RADIO_BW_12K5, &none);
		p25_engine_cfg_t cfg = mem->work.engine.cfg;
		cfg.rx_tgid = t->what == P25_FOLLOW_TUNE_GROUP || t->what == P25_FOLLOW_TUNE_TALK ? (uint16_t)t->id : 0;
		p25_engine_set(&mem->work.engine, &cfg);
		p25_engine_retuned(&mem->work.engine);
		p25_follow_retuned(f, now);
		if (!t->tuned && t->what == P25_FOLLOW_TUNE_TALK) {
			t->ptt = false;
			p25_follow_ptt(f, false, now);
			p25_follow_talk_end(f, now);
		}
	}
	if (t->ptt_pending) { t->ptt_pending = false; p25_follow_ptt(f, t->ptt, now); }
	if (t->talk_end) { t->talk_end = false; p25_follow_talk_end(f, now); }
	if (t->tx_kind == 0) p25_follow_tick(f, now);
	if (t->burst_pending && t->tx_kind == 0) {
		t->burst_pending = false;
		bool request = t->burst_permission == P25_TX_REQUEST;
		bool valid = t->tuned && t->burst_tune == t->want && t->want == t->done && f->view.state == P25_FOLLOW_CONTROL &&
			(request ? t->ptt && f->view.req == P25_REQ_WAIT : f->view.reg == P25_REG_WAIT || f->view.reg == P25_REG_AFF);
		if (valid && t->key != NULL && trunk_allowed(t->burst_hz, t->burst_permission)) {
			trunk_nac();
			bool ok = mem->work.engine.cfg.tx_nac != P25_NAC_ANY && p25_engine_tx_burst(&mem->work.engine, t->burst);
			if (ok) ok = t->key(t->burst_hz, true, t->key_ctx);
			if (ok) ok = p25_engine_tx_go(&mem->work.engine);
			if (ok) { t->tx_kind = 1; t->tx_hz = t->burst_hz; }
			else { p25_engine_tx_abort(&mem->work.engine); board.radio->tx_off(); p25_engine_tx_clear(&mem->work.engine); }
		}
	}
	if (t->ptt && t->tx_kind == 0 && t->tuned && t->want == t->done &&
	    f->view.req == P25_REQ_TALK && f->view.state == P25_FOLLOW_VOICE && t->what == P25_FOLLOW_TUNE_TALK) {
		uint32_t hz = f->view.talk_hz;
		if (t->key != NULL && trunk_allowed(hz, P25_TX_VOICE)) {
			trunk_nac();
			bool ok = mem->work.engine.cfg.tx_nac != P25_NAC_ANY && p25_engine_tx_prepare(&mem->work.engine);
			if (ok) { p25VoiceTx(true); ok = p25VoiceStreaming(); }
			if (ok) ok = t->key(hz, false, t->key_ctx);
			if (ok) ok = p25_engine_tx_go(&mem->work.engine);
			if (ok) { t->tx_kind = 2; t->tx_hz = hz; }
			else { p25VoiceTx(false); p25_engine_tx_abort(&mem->work.engine); board.radio->tx_off();
				p25_engine_tx_clear(&mem->work.engine); t->ptt = false; p25_follow_ptt(f, false, now); p25_follow_talk_end(f, now); }
		} else { t->ptt = false; p25_follow_ptt(f, false, now); p25_follow_talk_end(f, now); }
	}
	leave(&t->in_radio);
}
void radio_p25_trunk_keyer(radio_p25_key_t key, void *ctx)
{ if (radio_p25_trunked()) { mem->trunk.key = key; mem->trunk.key_ctx = ctx; } }
bool radio_p25_trunk_ptt(bool down)
{
	if (!radio_p25_trunked() || !current()) return false;
	radio_p25_trunk_t *t = &mem->trunk;
	if (down) {
		p25_follow_view_t view;
		p25_follow_view(&mem->work.follower, &view);
		if (!(p25_codeplug_tx_permissions(mem->number, &mem->channel) & P25_TX_REQUEST) ||
		    p25_follow_can_talk(&view, t->cfg.flags) != P25_TALK_OK) return false;
	}
	t->ptt = down; t->ptt_pending = true;
	return true;
}
void radio_p25_trunk_view(p25_follow_view_t *out)
{ if (out != NULL) { if (radio_p25_trunked()) p25_follow_view(&mem->work.follower, out); else memset(out, 0, sizeof *out); } }
uint32_t radio_p25_tx_frequency(void) { return radio_p25_trunked() ? mem->trunk.tx_hz : 0; }
bool radio_p25_tx_automatic(void) { return radio_p25_trunked() && mem->trunk.tx_kind == 1; }
bool radio_p25_trunk_failed(void)
{
	if (!radio_p25_trunked()) return true;
	uint8_t req = mem->work.follower.view.req;
	return !mem->trunk.ptt || req == P25_REQ_DENIED || req == P25_REQ_TIMEOUT || req == P25_REQ_NO_CHAN;
}
void radio_p25_trunk_status(uint8_t *state, uint8_t *registration, uint8_t *request)
{
	if (!radio_p25_trunked()) { *state = *registration = *request = 0; return; }
	radio_p25_trunk_t *t = &mem->trunk;
	if (!enter(&t->in_radio, &t->in_prov)) return;
	*state = mem->work.follower.view.state;
	*registration = mem->work.follower.view.reg;
	*request = mem->work.follower.view.req;
	leave(&t->in_radio);
}
#endif

bool radio_p25_params(const channel_t *ch, uint16_t number, p25_engine_cfg_t *cfg)
{
	p25_conv_cfg_t cc;
	if (ch == NULL || cfg == NULL || number == 0 || !p25_conv_load(number, ch, &cc)) return false;
	memset(cfg, 0, sizeof *cfg);
	cfg->silence = imbe_silence;
	cfg->rx_nac = cc.conv.rx_nac;
	cfg->tx_nac = cc.conv.tx_nac;
	if (cc.conv.squelch == P25_SQUELCH_SELECTIVE)
		cfg->rx_tgid = cc.target_unit ? (uint16_t)P25_TGID_EVERYONE : (uint16_t)cc.target;
	cfg->unit_id = cc.unit_id;
	cfg->tx_lc.lcf = cc.target_unit ? P25_LCF_UNIT : P25_LCF_GROUP;
	if (cc.target_unit) cfg->tx_lc.dst = cc.target;
	else cfg->tx_lc.tgid = (uint16_t)cc.target;
	cfg->talkaround = cc.talkaround;
	return true;
}

bool radio_p25_end(void)
{
	if (mem != NULL) {
		p25_engine_end(&mem->work.engine); // close synchronizes provider callbacks before the loan can return
#if CONFIG_P25_TRUNK
		if (radio_p25_trunked()) p25_follow_end(&mem->work.follower);
#endif
		board.radio->tx_off();
		p25VoiceEnd();
		mem = NULL;
	}
	return lease.vocoder == NULL || p25_workspace_release(&lease);
}

bool radio_p25_begin(const channel_t *ch, uint16_t number)
{
	bool kind = ch != NULL && ch->kind == CODEPLUG_KIND_P25;
#if CONFIG_P25_TRUNK
	kind = kind || (ch != NULL && ch->kind == CODEPLUG_KIND_P25_TRUNK);
#endif
	if (mem != NULL || lease.vocoder != NULL || !kind ||
	    !receive_band(ch->rx_hz) || ambeVoiceStreaming() || board.audio == NULL || board.radio == NULL ||
	    board.radio->tx_off == NULL ||
	    board.audio->start == NULL || board.audio->stop == NULL ||
	    (p25_engine_fit(board.air) & AIR_FIT_RX) == 0) return false;
	p25_engine_cfg_t cfg;
	channel_t stored;
	storage_lock();
	bool ok = codeplug_tagged_channel(number, &stored) && p25_channel_matches(&stored, ch);
	if (ok && ch->kind == CODEPLUG_KIND_P25) ok = radio_p25_params(&stored, number, &cfg);
	if (ok) ok = p25_workspace_acquire_bytes(&lease, board.vocoder, sizeof(radio_p25_mem_t));
	if (ok) {
		mem = lease.loan.data;
		mem->channel = stored;
		mem->number = number;
		mem->base_generation = codeplug_serial();
		mem->settings_generation = codeplug_kv_generation();
	}
#if CONFIG_P25_TRUNK
	if (ok && radio_p25_trunked() && !trunk_params(&cfg)) {
		mem = NULL;
		p25_workspace_release(&lease);
		ok = false;
	}
#endif
	storage_unlock();
	if (!ok) return false;
	p25VoiceInit(board.audio);
#if CONFIG_P25_TRUNK
	if (radio_p25_trunked()) memset(&mem->work.voice, 0, sizeof mem->work.voice);
#endif
	p25VoiceBegin(&mem->work.voice);
	p25_engine_hooks_t hooks = *p25VoiceHooks();
#if CONFIG_P25_TRUNK
	if (radio_p25_trunked()) {
		hooks.rx_unit = trunk_unit; hooks.rx_call = trunk_call;
		hooks.rx_start = trunk_rx_start; hooks.rx_update = trunk_rx_update; hooks.rx_voice = trunk_rx_voice;
		p25_follow_begin(&mem->work.follower, &mem->trunk.cfg, &follow_hooks, &mem->trunk, hal_millis());
	}
#endif
	if (!p25_engine_begin(&mem->work.engine, board.air, &cfg, &hooks, NULL) || !current()) {
		radio_p25_end();
		return false;
	}
	return true;
}

bool radio_p25_running(void) { return mem != NULL; }

bool radio_p25_poll(p25_engine_view_t *view)
{
	storage_lock();
	if (!current()) {
		radio_p25_end();
		storage_unlock();
		return false;
	}
#if CONFIG_P25_TRUNK
	if (radio_p25_trunked()) trunk_service();
#endif
	p25VoicePoll();
	if (view != NULL) p25_engine_view(&mem->work.engine, view);
#if CONFIG_P25_TRUNK
	if (view != NULL && radio_p25_trunked() && !trunk_playing()) view->rx_active = view->rx_played = false;
#endif
	storage_unlock();
	return true;
}

static bool transmit_allowed(void)
{
	return current() && !board_receive_only(mem->channel.rx_hz) && (mem->work.engine.session.fit & AIR_FIT_TX) != 0 &&
	       board_in_band(mem->channel.tx_hz, true) && receive_band(mem->channel.tx_hz) &&
	       (p25_codeplug_tx_permissions(mem->number, &mem->channel) & P25_TX_VOICE) != 0;
}

bool radio_p25_tx_prepare(void)
{
#if CONFIG_P25_TRUNK
	if (radio_p25_trunked()) return false; // only a current granted-call path can prepare trunked voice
#endif
	storage_lock();
	bool ok = transmit_allowed() && p25_engine_tx_prepare(&mem->work.engine);
	if (ok) {
		p25VoiceTx(true);
		ok = p25VoiceStreaming();
	}
	if (!ok) radio_p25_tx_abort();
	storage_unlock();
	return ok;
}

bool radio_p25_tx_go(void)
{
#if CONFIG_P25_TRUNK
	if (radio_p25_trunked()) return false;
#endif
	storage_lock();
	bool ok = transmit_allowed() && p25_engine_tx_go(&mem->work.engine);
	if (!ok) {
		radio_p25_tx_abort();
	}
	storage_unlock();
	return ok;
}

void radio_p25_tx_stop(void)
{
	if (mem != NULL) { p25VoiceTx(false); p25_engine_tx_stop(&mem->work.engine); }
}
void radio_p25_tx_abort(void)
{
	if (mem != NULL) {
		p25VoiceTx(false);
		p25_engine_tx_abort(&mem->work.engine);
		board.radio->tx_off();
#if CONFIG_P25_TRUNK
		if (radio_p25_trunked()) {
			mem->trunk.ptt = false; mem->trunk.ptt_pending = true;
			mem->trunk.burst_pending = false; mem->trunk.talk_end = true;
			mem->trunk.tx_kind = 0; mem->trunk.tx_hz = 0;
			p25_engine_tx_clear(&mem->work.engine);
		}
#endif
	}
}
uint8_t radio_p25_tx_state(void)
{
	return mem != NULL ? (uint8_t)p25_engine_tx_state(&mem->work.engine) : (uint8_t)AIR_TX_IDLE;
}
void radio_p25_tx_clear(void)
{
	if (mem != NULL && p25_engine_tx_state(&mem->work.engine) == AIR_TX_DONE) {
		board.radio->tx_off();
		p25VoiceTx(false);
		p25_engine_tx_clear(&mem->work.engine);
#if CONFIG_P25_TRUNK
		if (radio_p25_trunked()) {
			mem->trunk.tx_kind = 0; mem->trunk.tx_hz = 0;
			mem->trunk.talk_end = true;
		}
#endif
	}
}
#endif

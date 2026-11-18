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
		board.radio->tx_off();
		p25VoiceEnd();
		mem = NULL;
	}
	return lease.vocoder == NULL || p25_workspace_release(&lease);
}

bool radio_p25_begin(const channel_t *ch, uint16_t number)
{
	if (mem != NULL || lease.vocoder != NULL || ch == NULL || ch->kind != CODEPLUG_KIND_P25 ||
	    !receive_band(ch->rx_hz) || ambeVoiceStreaming() || board.audio == NULL || board.radio == NULL ||
	    board.radio->tx_off == NULL ||
	    board.audio->start == NULL || board.audio->stop == NULL ||
	    (p25_engine_fit(board.air) & AIR_FIT_RX) == 0) return false;
	p25_engine_cfg_t cfg;
	channel_t stored;
	storage_lock();
	bool ok = codeplug_tagged_channel(number, &stored) && p25_channel_matches(&stored, ch) &&
		  radio_p25_params(&stored, number, &cfg);
	if (ok) ok = p25_workspace_acquire_bytes(&lease, board.vocoder, sizeof(radio_p25_mem_t));
	if (ok) {
		mem = lease.loan.data;
		mem->channel = stored;
		mem->number = number;
		mem->base_generation = codeplug_serial();
		mem->settings_generation = codeplug_kv_generation();
	}
	storage_unlock();
	if (!ok) return false;
	p25VoiceInit(board.audio);
	p25VoiceBegin(&mem->work.voice);
	if (!p25_engine_begin(&mem->work.engine, board.air, &cfg, p25VoiceHooks(), NULL) || !current()) {
		radio_p25_end();
		return false;
	}
	return true;
}

bool radio_p25_running(void) { return mem != NULL; }

bool radio_p25_poll(p25_engine_view_t *view)
{
	if (!current()) {
		radio_p25_end();
		return false;
	}
	p25VoicePoll();
	if (view != NULL) p25_engine_view(&mem->work.engine, view);
	return true;
}

static bool transmit_allowed(void)
{
	return current() && (mem->work.engine.session.fit & AIR_FIT_TX) != 0 &&
	       board_in_band(mem->channel.tx_hz, true) && receive_band(mem->channel.tx_hz) &&
	       (p25_codeplug_tx_permissions(mem->number, &mem->channel) & P25_TX_VOICE) != 0;
}

bool radio_p25_tx_prepare(void)
{
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
	}
}
#endif

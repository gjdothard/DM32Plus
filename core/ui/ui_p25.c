// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// P25 monitoring and assigned-group selection. Drawing never touches the decoder loan.
#include <string.h>
#include "ui_p25.h"
#include "fmt.h"
#include "codeplug/codeplug_tag.h"
#if CONFIG_P25_UI

__attribute__((weak)) bool ui_svc_p25_view(p25_monitor_t *out)
{ memset(out, 0, sizeof *out); return false; }
__attribute__((weak)) bool ui_svc_p25_command(uint16_t channel, uint8_t op, uint16_t tg)
{ (void)channel; (void)op; (void)tg; return false; }
__attribute__((weak)) bool ui_svc_p25_favorite(uint16_t channel, uint16_t tg, bool remove)
{ (void)channel; (void)tg; (void)remove; return false; }
#if CONFIG_P25_TRUNK
__attribute__((weak)) uint8_t ui_svc_p25_site(const p25_monitor_t *m, char *out, size_t cap)
{ (void)m; if (cap) out[0] = '\0'; return P25_SITE_UNKNOWN; }
#endif

static void site_text(const p25_monitor_t *m, char *text, size_t cap, bool detail)
{
	(void)detail;
	sbuf_t b;
	sb_init(&b, text, cap);
#if CONFIG_P25_TRUNK
	char name[CODEPLUG_NAME_LEN + 1];
	uint8_t state = ui_svc_p25_site(m, name, sizeof name);
	if (state == P25_SITE_UNAVAILABLE) { sb_str(&b, TR("Site unavailable")); return; }
	if (state == P25_SITE_MISMATCH) { sb_str(&b, TR("Site mismatch")); return; }
	if (name[0]) {
		if (!detail || state != P25_SITE_CONFIRMED) {
			sb_str(&b, state == P25_SITE_CONFIRMED ? TR("Site") : TR("Candidate")); sb_str(&b, ": ");
		}
		sb_str(&b, name); return;
	}
#endif
	if (m->state >= 2 && m->rfss != 0xFF && m->site != 0xFF) {
		sb_str(&b, TR("Site")); sb_chr(&b, ' '); sb_u32(&b, m->rfss, 1);
		sb_str(&b, " / "); sb_u32(&b, m->site, 1);
	} else sb_str(&b, m->state >= 2 ? TR("Unknown site") : TR("Searching for control"));
}

void ui_p25_talkgroup_step(int16_t steps)
{
	if (!ui_svc_p25_command(ui_view()->channel_number, P25_MON_STEP, (uint16_t)steps))
		ui_toast(TR("P25 request unavailable"));
}

static bool snapshot(p25_monitor_t *m)
{
	return ui_svc_p25_view(m) && m->channel == ui_view()->channel_number && ui_view()->ch.mode == RADIO_MODE_P25;
}

static const char *status(const p25_monitor_t *m)
{
	if (!(m->flags & P25_MON_ACTIVE)) return TR("P25 unavailable");
	if (m->flags & P25_MON_RX) return m->flags & P25_MON_ENC ? TR("Encrypted call") : TR("Receiving voice");
	if (!(m->flags & P25_MON_TRUNK)) return TR("Waiting for P25");
	if (m->state == 3) return TR("Following voice");
	if (m->state != 2) return TR("Searching for control");
	return TR("Control channel locked");
}

static const char *compact_status(const p25_monitor_t *m)
{
	if (!(m->flags & P25_MON_ACTIVE)) return TR("Unavailable");
	if (m->flags & P25_MON_RX) return m->flags & P25_MON_ENC ? TR("Encrypted") : TR("Voice");
	if (!(m->flags & P25_MON_TRUNK)) return TR("Waiting");
	if (m->state == 3) return TR("Voice");
	return m->state == 2 ? TR("Control") : TR("Searching");
}

static bool favorite(const p25_monitor_t *m, uint16_t tg)
{
	for (unsigned i = 0; i < m->favorite_count; i++) if (m->favorites[i] == tg) return true;
	return false;
}

static uint16_t selected_group(const p25_monitor_t *m)
{
	return m->listen == P25_LISTEN_HOLD && !(m->flags & P25_MON_RX) &&
		!((m->flags & P25_MON_TRUNK) && m->state == 3) ? m->held_tg : 0;
}

static void group_text(char *text, size_t len, uint16_t tg)
{
	sbuf_t b;
	sb_init(&b, text, len); sb_str(&b, "TG "); sb_u32(&b, tg, 1);
}

static void tx_text(char *text, size_t len, const p25_monitor_t *m)
{
	sbuf_t b;
	sb_init(&b, text, len);
	sb_str(&b, m->flags & P25_MON_TX_UNIT ? "ID " : "TG ");
	sb_u32(&b, m->tx_target, 1);
}

static void line(const font_t *font, int *y, const char *text, color_t color)
{
	char fit[64];
	if (*y + font->h > ui_layout.hint_y) return;
	gfx_text_fit(font, text, gfx_width() - 8, fit, sizeof fit);
	gfx_text(font, 4, *y, fit, color);
	*y += font->h + 3;
}

static void __attribute__((noinline)) home_standard(const radio_view_t *v, const ui_state_t *s)
{
	p25_monitor_t m;
	if (!snapshot(&m)) memset(&m, 0, sizeof m);
	char text[64];
	sbuf_t b;
	const font_t *f = ui_layout.small;
	gfx_clear(ui_col.bg);
	int y = ui_draw_top(v, s, "P25", v->ch.name) + 3;
	line(f, &y, v->state == RS_TX ? TR("Transmitting") : status(&m), v->state == RS_TX ? ui_col.tx : ui_col.rx);
	if (v->state == RS_TX) {
		tx_text(text, sizeof text, &m);
		line(ui_layout.large, &y, text, ui_col.tx);
		ui_draw_banner(s);
		ui_draw_bottom(s, TR("Transmitting"));
		return;
	}
	if (gfx_height() >= 128) y += 4;
	uint16_t selected = selected_group(&m);
	if (m.recent_count || selected) {
		const p25_monitor_call_t *c = !selected && m.recent_count ? &m.recent[0] : NULL;
		uint16_t tg = selected ? selected : c->tg;
		bool unit = c && (c->flags & P25_MON_UNIT);
		if (gfx_height() >= 200) line(f, &y, selected || (m.flags & P25_MON_RX) ? TR("Talkgroup") : TR("Last heard"), ui_col.dim);
		if (unit) str_copy(text, sizeof text, TR("Private call"));
		else group_text(text, sizeof text, tg);
		line(gfx_height() >= 128 ? ui_layout.large : f, &y, text, ui_col.fg);
		if (!unit && gfx_height() >= 128) {
			contact_t contact;
			if (codeplug_contact_find_protocol(tg, true, 2, &contact) && !ui_name_is_id(contact.name, tg, false))
				line(ui_layout.medium, &y, contact.name, ui_col.fg);
		}
		if (gfx_height() >= 128) {
			sb_init(&b, text, sizeof text); sb_str(&b, TR("Radio ID")); sb_chr(&b, ' ');
			if (c && c->src) sb_u32(&b, c->src, 1); else sb_str(&b, "--");
			line(f, &y, text, ui_col.dim);
		}
	} else line(f, &y, TR("No calls heard"), ui_col.dim);
	if (m.flags & P25_MON_TRUNK) { site_text(&m, text, sizeof text, false); line(f, &y, text, ui_col.focus); }
	if (gfx_height() >= 128) {
		sb_init(&b, text, sizeof text); sb_str(&b, TR("Tuned")); sb_chr(&b, ' ');
		sb_freq(&b, m.tuned_hz ? m.tuned_hz : v->ch.rx_hz); line(f, &y, text, ui_col.fg);
		if (m.flags & P25_MON_TRUNK) {
			sb_init(&b, text, sizeof text); sb_str(&b, TR("Control")); sb_chr(&b, ' ');
			if (m.cc_hz) sb_freq(&b, m.cc_hz); else sb_str(&b, "--");
			line(f, &y, text, ui_col.dim);
		}
		sb_init(&b, text, sizeof text);
		if (m.listen == P25_LISTEN_HOLD) { sb_str(&b, TR("Hold")); sb_str(&b, " TG "); sb_u32(&b, m.held_tg, 1); }
		else sb_str(&b, m.listen == P25_LISTEN_ALL ? TR("All talkgroups") :
			m.listen == P25_LISTEN_FAVORITES ? TR("Favorites only") : TR("Programmed groups"));
		line(f, &y, text, ui_col.fg);
		if ((m.flags & P25_MON_RX_ONLY) || v->ch.rx_only) line(f, &y, TR("RX only"), ui_col.warn);
		sb_init(&b, text, sizeof text); sb_str(&b, TR("Signal")); sb_chr(&b, ' ');
		sb_i32(&b, v->rssi_dbm); sb_str(&b, " dBm"); line(f, &y, text, ui_col.dim);
	}
	ui_draw_banner(s);
	ui_draw_bottom(s, (m.flags & P25_MON_RX_ONLY) || v->ch.rx_only ? TR("RX only  # Recent") : TR("OK Menu  # Recent"));
}

#if CONFIG_DUAL_WATCH && CONFIG_UI_CARD_MENU
static void card_text(const font_t *font, int x, int y, int width, const char *text, color_t color)
{
	char fit[64];
	gfx_text_fit(font, text, width, fit, sizeof fit);
	gfx_text(font, x, y, fit, color);
}

static void card_pair(int y, const char *label, const char *value, color_t color)
{
	const font_t *font = &font_6x12;
	int width = gfx_width(), right = gfx_text_width(font, value);
	card_text(font, 6, y, width - right - 24, label, ui_col.dim);
	gfx_text(font, width - right - 6, y, value, color);
}

static void card_state(const p25_monitor_t *m, const radio_view_t *v)
{
	int x = gfx_width() / 2 - 20, y = 56;
	color_t color = ui_col.focus;
	if (v->state == RS_TX || v->state == RS_TX_DENIED) {
		gfx_text(&font_6x12, x, y, v->state == RS_TX ? "TX" : "!", ui_col.tx);
	} else if ((m->flags & P25_MON_RX) || m->state == 3) {
		// Speaker: voice reception or a voice grant, separate from the privacy indicator.
		gfx_fill(x, y + 4, 3, 5, color);
		gfx_vline(x + 3, y + 2, 9, color); gfx_vline(x + 4, y + 1, 11, color);
		gfx_vline(x + 7, y + 3, 7, color); gfx_vline(x + 10, y + 1, 11, color);
	} else if ((m->flags & P25_MON_ACTIVE) && m->state == 2) {
		// A control-channel link, rather than the keypad/privacy padlock.
		gfx_rect(x, y + 1, 7, 7, color); gfx_rect(x + 5, y + 5, 7, 7, color);
	} else {
		color = ui_col.warn;
		gfx_hline(x + 2, y, 4, color); gfx_hline(x + 2, y + 7, 4, color);
		gfx_vline(x, y + 2, 4, color); gfx_vline(x + 7, y + 2, 4, color);
		gfx_pixel(x + 1, y + 1, color); gfx_pixel(x + 6, y + 1, color);
		gfx_pixel(x + 1, y + 6, color); gfx_pixel(x + 6, y + 6, color);
		for (int i = 0; i < 5; i++) gfx_pixel(x + 7 + i, y + 7 + i, color);
	}
	unsigned bars = !(m->flags & P25_MON_ACTIVE) ? 0 : v->rssi_dbm >= -75 ? 4 :
		v->rssi_dbm >= -90 ? 3 : v->rssi_dbm >= -105 ? 2 : v->rssi_dbm >= -120 ? 1 : 0;
	gfx_icon_bars(x + 20, y, 18, 12, 4, bars, color, ui_col.dim);
}

static void __attribute__((noinline)) home_card_body(const radio_view_t *v, const ui_state_t *s)
{
	p25_monitor_t m;
	if (!snapshot(&m)) memset(&m, 0, sizeof m);
	const font_t *meta = &font_6x12, *font = ui_layout.small;
	int width = gfx_width();
	char text[64];
	sbuf_t b;
	card_text(font, 6, 32, width - 12, v->ch.name, ui_col.fg);
	card_text(meta, 6, 56, width - 80, m.flags & P25_MON_TRUNK ? "P25 TRUNK" : "P25", ui_col.dim);
	if ((m.flags & P25_MON_RX_ONLY) || v->ch.rx_only) {
		int tw = gfx_text_width(meta, TR("RX only"));
		gfx_text(meta, width - tw - 6, 56, TR("RX only"), ui_col.warn);
	}
	if (m.flags & P25_MON_TRUNK) {
		card_state(&m, v);
		site_text(&m, text, sizeof text, false);
		card_text(meta, 6, 74, width - 12, text, ui_col.focus);
	} else {
		sb_init(&b, text, sizeof text); sb_i32(&b, v->rssi_dbm); sb_str(&b, " dBm");
		const char *state = v->state == RS_TX ? TR("Transmitting") : v->state == RS_TX_DENIED ? TR("TX denied") : status(&m);
		int signal_width = gfx_text_width(meta, text);
		card_text(meta, 6, 74, width - signal_width - 24, state,
			v->state == RS_TX || v->state == RS_TX_DENIED ? ui_col.tx : ui_col.focus);
		gfx_text(meta, width - signal_width - 6, 74, text, ui_col.dim);
	}
	// The call card uses the same surfaces and typography as the other card pages.
	gfx_round_fill(2, 94, width - 4, 108, UI_CARD_SURFACE);
	bool current = (m.flags & P25_MON_RX) || ((m.flags & P25_MON_TRUNK) && m.state == 3);
	uint16_t selected = selected_group(&m);
	card_text(meta, 10, 101, width - 20, v->state == RS_TX ? TR("Transmit target") :
		current || selected ? TR("Talkgroup") : TR("Last heard"), ui_col.dim);
	const p25_monitor_call_t *call = !selected && m.recent_count ? &m.recent[0] : NULL;
	if (v->state == RS_TX) tx_text(text, sizeof text, &m);
	else if (selected) group_text(text, sizeof text, selected);
	else if (call && (call->flags & P25_MON_UNIT)) str_copy(text, sizeof text, TR("Private call"));
	else if (call) group_text(text, sizeof text, call->tg);
	else str_copy(text, sizeof text, "--");
	card_text(ui_layout.large, 10, 119, width - 20, text, ui_col.fg);
	if (v->state == RS_TX) {
		contact_t contact;
		if (codeplug_contact_find_protocol(m.tx_target, !(m.flags & P25_MON_TX_UNIT), 2, &contact))
			card_text(font, 10, 157, width - 20, contact.name, ui_col.fg);
	} else if (selected || (call && call->tg)) {
		contact_t contact;
		uint16_t tg = selected ? selected : call->tg;
		if (codeplug_contact_find_protocol(tg, true, 2, &contact) && !ui_name_is_id(contact.name, tg, false))
			card_text(font, 10, 157, width - 20, contact.name, ui_col.fg);
	} else if (!call && !selected) card_text(meta, 10, 160, width - 20, TR("No calls heard"), ui_col.dim);
	sb_init(&b, text, sizeof text); sb_str(&b, TR("Radio ID")); sb_chr(&b, ' ');
	if (call && call->src) sb_u32(&b, call->src, 1); else sb_str(&b, "--");
	if (call && (call->flags & P25_MON_ENC)) { sb_str(&b, "  "); sb_str(&b, TR("Encrypted call")); }
	if (v->state == RS_TX) str_copy(text, sizeof text, TR("Programmed target"));
	card_text(meta, 10, 184, width - 20, text, ui_col.dim);
	sb_init(&b, text, sizeof text); sb_freq(&b, m.tuned_hz ? m.tuned_hz : v->ch.rx_hz); sb_str(&b, " MHz");
	card_pair(216, current && (m.flags & P25_MON_TRUNK) ? TR("Voice") : TR("Tuned"), text, ui_col.fg);
	if (m.flags & P25_MON_TRUNK) {
		sb_init(&b, text, sizeof text);
		if (m.cc_hz) { sb_freq(&b, m.cc_hz); sb_str(&b, " MHz"); } else sb_str(&b, "--");
		card_pair(238, TR("Control"), text, ui_col.fg);
	}
	sb_init(&b, text, sizeof text);
	if (m.listen == P25_LISTEN_HOLD) { sb_str(&b, TR("Hold")); sb_str(&b, " TG "); sb_u32(&b, m.held_tg, 1); }
	else sb_str(&b, m.listen == P25_LISTEN_ALL ? TR("All talkgroups") :
		m.listen == P25_LISTEN_FAVORITES ? TR("Favorites only") : TR("Programmed groups"));
	card_text(meta, 6, 264, width - 12, text, ui_col.focus);
	if (!(m.flags & P25_MON_RX_ONLY) && !v->ch.rx_only) {
		char target[32];
		tx_text(target, sizeof target, &m);
		sb_init(&b, text, sizeof text); sb_str(&b, "TX "); sb_str(&b, target);
		int tw = gfx_text_width(meta, text);
		gfx_fill(width - tw - 12, 262, tw + 12, meta->h + 4, UI_CARD_INK);
		gfx_text(meta, width - tw - 6, 264, text, ui_col.dim);
	}
}
static void __attribute__((noinline)) home_card(const radio_view_t *v, const ui_state_t *s)
{
	// Keep the call snapshot off the shared status bar's deepest stack path.
	ui_draw_card_begin(s, NULL);
	home_card_body(v, s);
	ui_draw_card_footer(s, TR("* P25"), TR("# Recent"));
	if (s->toast || s->locked || v->state == RS_TX_DENIED) ui_draw_bottom(s, TR("OK Menu  # Recent"));
}
#endif

void ui_p25_home(const radio_view_t *v, const ui_state_t *s)
{
#if CONFIG_DUAL_WATCH && CONFIG_UI_CARD_MENU
	if (ui_card_layout(s)) { home_card(v, s); return; }
#endif
	home_standard(v, s);
}

static void command(const p25_monitor_t *m, uint8_t op, uint16_t tg)
{
	if (!ui_svc_p25_command(m->channel, op, tg)) ui_toast(TR("P25 request unavailable"));
	ui_dirty();
}

static uint16_t listen_count(void) {
#if CONFIG_DMR && CONFIG_DMR_TALKGROUP_ENTRIES
	return 8;
#else
	return 7;
#endif
}
static void listen_item(uint16_t i, ui_item_t *it)
{
	static const char *const labels[] = { TRN("Recent calls"), TRN("All talkgroups"), TRN("Favorites only"),
		TRN("Hold last talkgroup"), TRN("Programmed groups"), TRN("System details"), TRN("Favorite list"), TRN("Talkgroups") };
	p25_monitor_t m;
	bool ok = snapshot(&m);
	item_num_set(it, i + 1);
	str_copy(it->label, sizeof it->label, tr(labels[i]));
	if (!ok || (i > 0 && i < 5 && !(m.flags & P25_MON_TRUNK))) it->flags |= UI_ITEM_DIM;
	if (i == 1 && m.listen == P25_LISTEN_ALL) str_copy(it->value, sizeof it->value, TR("On"));
	if (i == 2 && m.listen == P25_LISTEN_FAVORITES) str_copy(it->value, sizeof it->value, TR("On"));
	if (i == 3 && m.listen == P25_LISTEN_HOLD) group_text(it->value, sizeof it->value, m.held_tg);
}
static void listen_select(uint16_t i, bool hold)
{
	(void)hold;
	p25_monitor_t m;
	if (i == 7) {
#if CONFIG_DMR && CONFIG_DMR_TALKGROUP_ENTRIES
		ui_push(SCR_P25_TALKGROUPS, 1);
#else
		ui_toast(TR("P25 request unavailable"));
#endif
		return;
	}
	if (i == 0 || i == 6) { ui_push(SCR_P25_RECENT, i == 6); return; }
	if (i == 5) { ui_push(SCR_P25_SYSTEM, 0); return; }
	if (!snapshot(&m) || !(m.flags & P25_MON_TRUNK)) { ui_toast(TR("Select a P25 trunked channel")); return; }
	if (i == 2 && !m.favorite_count) { ui_toast(TR("Add a recent talkgroup first")); return; }
	if (i == 3 && (!m.recent_count || !m.recent[0].tg)) { ui_toast(TR("No group call to hold")); return; }
	command(&m, i == 1 ? P25_MON_ALL : i == 2 ? P25_MON_FAVORITES : i == 3 ? P25_MON_HOLD : P25_MON_PROGRAMMED,
		i == 3 ? m.recent[0].tg : 0);
	if (i == 3) ui_go_home();
}

static uint16_t recent_count(void)
{
	p25_monitor_t m;
	return snapshot(&m) ? (ui_frame()->arg ? m.favorite_count : m.recent_count) : 0;
}
static void recent_item(uint16_t i, ui_item_t *it)
{
	p25_monitor_t m;
	if (!snapshot(&m) || i >= (ui_frame()->arg ? m.favorite_count : m.recent_count)) return;
	uint16_t tg = ui_frame()->arg ? m.favorites[i] : m.recent[i].tg;
	contact_t contact;
	if (tg && codeplug_contact_find_protocol(tg, true, 2, &contact)) str_copy(it->label, sizeof it->label, contact.name);
	else if (tg) group_text(it->label, sizeof it->label, tg);
	else str_copy(it->label, sizeof it->label, TR("Private call"));
	sbuf_t b;
	sb_init(&b, it->value, sizeof it->value);
	if (tg) { sb_str(&b, "TG "); sb_u32(&b, tg, 1); }
	if (favorite(&m, tg)) sb_str(&b, " *");
	if (!ui_frame()->arg) {
		sb_str(&b, "\n"); sb_str(&b, TR("Radio ID")); sb_chr(&b, ' ');
		if (m.recent[i].src) sb_u32(&b, m.recent[i].src, 1); else sb_str(&b, "--");
		sb_str(&b, "  "); sb_age(&b, ui_st.now_ms - m.recent[i].ms);
	}
}
static void recent_select(uint16_t i, bool hold)
{
	(void)hold;
	p25_monitor_t m;
	if (!snapshot(&m) || i >= (ui_frame()->arg ? m.favorite_count : m.recent_count)) return;
	uint16_t tg = ui_frame()->arg ? m.favorites[i] : m.recent[i].tg;
	if (!(m.flags & P25_MON_TRUNK) || !tg) { ui_toast(TR("No group call to hold")); return; }
	command(&m, P25_MON_HOLD, tg);
	ui_go_home();
}
static void recent_context(uint16_t i)
{
	p25_monitor_t m;
	if (!snapshot(&m) || i >= (ui_frame()->arg ? m.favorite_count : m.recent_count)) return;
	uint16_t tg = ui_frame()->arg ? m.favorites[i] : m.recent[i].tg;
	if (!(m.flags & P25_MON_TRUNK) || !tg) { ui_toast(TR("No group call to hold")); return; }
	if (!favorite(&m, tg) && m.favorite_count == P25_MONITOR_FAVORITES) { ui_toast(TR("Favorite list full")); return; }
	command(&m, favorite(&m, tg) ? P25_MON_REMOVE : P25_MON_ADD, tg);
}

static const char *recent_title(void) { return ui_frame()->arg ? TR("Favorite list") : TR("Recent calls"); }
static void __attribute__((noinline)) recent_body(int y, bool cards)
{
	p25_monitor_t m;
	bool ok = snapshot(&m);
	ui_frame_t *frame = ui_frame();
	bool favorites = frame->arg != 0;
	unsigned count = ok ? (favorites ? m.favorite_count : m.recent_count) : 0;
	const font_t *font = ui_layout.small;
	const font_t *meta = cards ? &font_6x12 : font;
	int pitch = font->h + (favorites ? 0 : meta->h) + 6;
	unsigned rows = (unsigned)(ui_layout.hint_y > y ? (ui_layout.hint_y - y) / pitch : 0);
	if (!rows) rows = 1;
	if (frame->focus >= count) frame->focus = count ? count - 1 : 0;
	if (frame->focus < frame->top) frame->top = frame->focus;
	if (frame->focus >= frame->top + rows) frame->top = frame->focus - rows + 1;
	if (!count) line(font, &y, TR("No calls or favorites"), ui_col.dim);
	for (unsigned i = frame->top; i < count && i < frame->top + rows; i++) {
		bool selected = i == frame->focus;
		color_t color = selected && !cards ? (gfx_bpp() == 1 ? ui_col.bg : ui_col.focus_fg) : ui_col.fg;
		if (selected) {
			if (cards) {
				gfx_round_fill(1, y - 1, gfx_width() - 2, pitch - 2, UI_CARD_SELECTED);
				gfx_vline(0, y, pitch - 3, ui_col.focus);
			} else gfx_fill(0, y, gfx_width(), pitch - 2, ui_col.focus);
		}
		char text[64];
		sbuf_t b;
		uint16_t tg = favorites ? m.favorites[i] : m.recent[i].tg;
		sb_init(&b, text, sizeof text);
		if (tg) { sb_str(&b, "TG "); sb_u32(&b, tg, 1); }
		else sb_str(&b, TR("Private call"));
		if (favorite(&m, tg)) sb_str(&b, " *");
		contact_t contact;
		if (tg && codeplug_contact_find_protocol(tg, true, 2, &contact) && !ui_name_is_id(contact.name, tg, false)) {
			sb_chr(&b, ' '); sb_str(&b, contact.name);
		}
		line(font, &y, text, color);
		if (!favorites) {
			sb_init(&b, text, sizeof text); sb_str(&b, TR("Radio ID")); sb_chr(&b, ' ');
			if (m.recent[i].src) sb_u32(&b, m.recent[i].src, 1); else sb_str(&b, "--");
			sb_str(&b, "  "); sb_age(&b, ui_st.now_ms - m.recent[i].ms);
			line(meta, &y, text, selected ? color : ui_col.dim);
		} else y += 3;
	}
}
static void recent_draw(void)
{
#if CONFIG_DUAL_WATCH && CONFIG_UI_CARD_MENU
	if (ui_card_layout(&ui_st)) {
		int y = ui_draw_card_begin(&ui_st, recent_title());
		recent_body(y, true);
		ui_draw_card_footer(&ui_st, TR("OK Hold"), TR("# Favorite"));
		return;
	}
#endif
	gfx_clear(ui_col.bg);
	int y = ui_draw_top(ui_view(), &ui_st, "P25", recent_title()) + 3;
	recent_body(y, false);
	ui_draw_banner(&ui_st);
	ui_draw_bottom(&ui_st, TR("OK Hold  # Favorite"));
}

static uint16_t system_count(void) { return ui_frame()->arg ? 4 : ui_view()->ch.kind == CODEPLUG_KIND_P25_TRUNK ? 8 : 7; }
static void hex(sbuf_t *b, uint32_t value, unsigned digits)
{
	while (digits) { digits--; sb_chr(b, "0123456789ABCDEF"[(value >> (digits * 4)) & 15u]); }
}
static void system_item(uint16_t i, ui_item_t *it)
{
	static const uint8_t overview[] = {0, 11, 1, 3, 4, 5, 10, 8};
	static const uint8_t decode[] = {2, 6, 7, 9};
	i = ui_frame()->arg ? decode[i] : overview[i + (i && ui_view()->ch.kind != CODEPLUG_KIND_P25_TRUNK ? 1 : 0)];
	static const char *const labels[] = { TRN("Status"), TRN("Control"), TRN("Tuned"), "WACN / SYS", "RFSS / SITE", "NAC",
		TRN("Valid blocks"), TRN("CRC failures"), TRN("Transmit"), TRN("Last skip"), TRN("Signal"), TRN("Site") };
	p25_monitor_t m;
	bool ok = snapshot(&m);
	str_copy(it->label, sizeof it->label, tr(labels[i]));
	sbuf_t b;
	sb_init(&b, it->value, sizeof it->value);
	if (!ok) { sb_str(&b, "--"); return; }
	switch (i) {
	case 0:
		sb_str(&b, TEXT_ALT_OPEN); sb_str(&b, status(&m));
		sb_str(&b, TEXT_ALT_NEXT); sb_str(&b, compact_status(&m)); sb_str(&b, TEXT_ALT_CLOSE);
		break;
	case 1: if (m.cc_hz) sb_freq(&b, m.cc_hz); else sb_str(&b, "--"); break;
	case 2: sb_freq(&b, m.tuned_hz); break;
	case 3:
		if (m.wacn != 0xFFFFFFFF) hex(&b, m.wacn, 5); else sb_str(&b, "--");
		sb_str(&b, " / "); if (m.sysid != 0xFFFF) hex(&b, m.sysid, 3); else sb_str(&b, "--"); break;
	case 4:
		if (m.rfss != 0xFF) sb_u32(&b, m.rfss, 1); else sb_str(&b, "--");
		sb_str(&b, " / "); if (m.site != 0xFF) sb_u32(&b, m.site, 1); else sb_str(&b, "--"); break;
	case 5: if (m.nac <= 0xFFF && (m.flags & P25_MON_TRUNK ? m.state >= 2 : m.recent_count != 0)) hex(&b, m.nac, 3); else sb_str(&b, "--"); break;
	case 6: sb_u32(&b, m.blocks, 1); break;
	case 7: sb_u32(&b, m.crc_fail, 1); break;
	case 8: sb_str(&b, m.flags & P25_MON_RX_ONLY ? TR("RX only") : TR("Channel settings")); break;
	case 9: sb_str(&b, m.refused == 1 ? TR("Encrypted call skipped") : m.refused == 2 ? TR("Phase 2 call skipped") :
		m.refused == 3 ? TR("Missing channel plan") : TR("None")); break;
	case 10: sb_i32(&b, ui_view()->rssi_dbm); sb_str(&b, " dBm"); break;
	case 11: if (m.flags & P25_MON_TRUNK) site_text(&m, it->value, sizeof it->value, true); else sb_str(&b, "--"); break;
	}
}
static void system_context(uint16_t i)
{
	(void)i;
	ui_frame()->arg ^= 1; ui_frame()->focus = ui_frame()->top = 0; ui_dirty();
}

const ui_scr_def_t ui_p25_listen_screen = {
	.name = "p25-listen", .flags = SF_LIST | SF_NUMBERED | SF_FIELD, .prefix = TRN("P25 listening"),
	.count = listen_count, .item = listen_item, .select = listen_select,
};
const ui_scr_def_t ui_p25_recent_screen = {
	.name = "p25-recent", .flags = SF_LIST | SF_FIELD, .prefix = "P25", .title = recent_title,
	.hint = TRN("OK Hold  # Favorite"), .empty = TRN("No calls or favorites"),
	.count = recent_count, .item = recent_item, .select = recent_select, .context = recent_context, .draw = recent_draw,
};
const ui_scr_def_t ui_p25_system_screen = {
	.name = "p25-system", .flags = SF_LIST | SF_FIELD, .prefix = TRN("P25 system"),
	.count = system_count, .item = system_item, .context = system_context, .hint = TRN("# Details  Back"),
};
#endif

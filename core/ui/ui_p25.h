// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef UI_P25_H
#define UI_P25_H
#include "ui_nav.h"
#include "radio/p25_monitor.h"
#if CONFIG_P25_UI
bool ui_svc_p25_view(p25_monitor_t *out);
#if CONFIG_P25_TRUNK
#include "p25/p25_codeplug.h"
uint8_t ui_svc_p25_site(const p25_monitor_t *monitor, char *out, size_t cap);
#endif
bool ui_svc_p25_command(uint16_t channel, uint8_t op, uint16_t tg);
bool ui_svc_p25_favorite(uint16_t channel, uint16_t tg, bool remove);
void ui_p25_home(const radio_view_t *v, const ui_state_t *s);
void ui_p25_talkgroup_step(int16_t steps);
bool ui_p25_quick_tile(uint8_t kind, char *label, size_t label_len, char *value, size_t value_len);
bool ui_p25_quick_activate(uint8_t kind);
const char *ui_p25_quick_hint(uint8_t kind);
void ui_p25_quick_context(char *text, size_t cap, bool permission);
extern const ui_scr_def_t ui_p25_listen_screen, ui_p25_recent_screen, ui_p25_system_screen;
#endif
#endif

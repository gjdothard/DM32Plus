// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef UI_P25_H
#define UI_P25_H
#include "ui_nav.h"
#include "radio/p25_monitor.h"
#if CONFIG_P25_UI
bool ui_svc_p25_view(p25_monitor_t *out);
bool ui_svc_p25_command(uint16_t channel, uint8_t op, uint16_t tg);
bool ui_svc_p25_favorite(uint16_t channel, uint16_t tg, bool remove);
void ui_p25_home(const radio_view_t *v, const ui_state_t *s);
extern const ui_scr_def_t ui_p25_listen_screen, ui_p25_recent_screen, ui_p25_system_screen;
#endif
#endif

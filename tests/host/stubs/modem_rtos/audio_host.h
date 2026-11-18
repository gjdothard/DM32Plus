// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Keep platform definitions; replace CPU instructions for linked audio/APRS tests
#ifndef AUDIO_HOST_PLATFORM_H
#define AUDIO_HOST_PLATFORM_H
#define hrc_irq_save audio_target_irq_save
#define hrc_irq_restore audio_target_irq_restore
#define hrc_in_isr audio_target_in_isr
#define hrc_barrier audio_target_barrier
#define hrc_timer_ack audio_target_timer_ack
#include "platform.h"
#undef hrc_irq_save
#undef hrc_irq_restore
#undef hrc_in_isr
#undef hrc_barrier
#undef hrc_timer_ack
uint32_t audio_host_irq_save(void);
void audio_host_irq_restore(uint32_t saved);
bool audio_host_in_isr(void);
void audio_host_barrier(void);
void audio_host_timer_ack(uint8_t timer);
#define hrc_irq_save audio_host_irq_save
#define hrc_irq_restore audio_host_irq_restore
#define hrc_in_isr audio_host_in_isr
#define hrc_barrier audio_host_barrier
#define hrc_timer_ack audio_host_timer_ack
#endif

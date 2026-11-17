// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Copyright (c) 2026 The RadioFoundry Authors
// FreeRTOS settings this radio changes from platform/hrc7000/FreeRTOSConfig.h (read before its defaults)
#ifndef FREERTOS_BOARD_H
#define FREERTOS_BOARD_H

// The RTOS heap holds every task stack and queue. Main RAM is this radio's limit (config.mk), and the platform's 40 KB
// leaves about 13 KB of it unused: the ten tasks' stacks are 22.8 KB (make stack-check), their control blocks about
// 1.1 KB, the FM receive ring 2 KB and the queues and locks about 1.3 KB, 27.3 KB in all, and a zone share send takes
// 1 KB for a moment (core/ui/ui_svc_zone.c). 32 KB budgets about 3.7 KB spare, above the 2 KB
// tests/hil/test_task_stacks.py asks for, and gives the rest to main RAM (AES sessions need the next 1 KB after
// the air MTU's lists took 34 down to 33). Recheck that budget on hardware; the debug link's tasks view reports
// the least free since start (tasks.heap_min_free).
// The enabled clock needs 24 bytes beyond the previous RAM budget. Leave 40 more bytes for alignment and headroom.
#if CONFIG_RTC
#define configTOTAL_HEAP_SIZE	(32 * 1024 - 64)
#else
#define configTOTAL_HEAP_SIZE	(32 * 1024)
#endif

#endif

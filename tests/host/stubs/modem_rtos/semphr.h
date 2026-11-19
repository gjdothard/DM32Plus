// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Declarations for compiling board attachment code; host tests do not take locks
#ifndef MODEM_TEST_SEMPHR_H
#define MODEM_TEST_SEMPHR_H
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t ticks);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t mutex);
#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#ifndef MODEM_TEST_TASK_H
#define MODEM_TEST_TASK_H
#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *ctx,
		      uint32_t priority, TaskHandle_t *handle);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
void xTaskNotifyGive(TaskHandle_t task);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
#endif

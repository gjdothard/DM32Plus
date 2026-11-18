// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// The modem glue's RTOS calls only; host tests never start a task or scheduler
#ifndef MODEM_TEST_FREERTOS_H
#define MODEM_TEST_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define configMAX_PRIORITIES 8
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR(woken) ((void)(woken))
#endif

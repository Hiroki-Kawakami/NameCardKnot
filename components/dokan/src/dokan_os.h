/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Threading primitives: a recursive mutex and task identity. pthread on host,
 * FreeRTOS on device. The recursive mutex lets an event callback re-enter the
 * public API while the session lock is held.
 */

#pragma once
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef SemaphoreHandle_t dokan_mutex_t;
typedef TaskHandle_t      dokan_task_t;

static inline void dokan_mutex_init(dokan_mutex_t *m)    { *m = xSemaphoreCreateRecursiveMutex(); }
static inline void dokan_mutex_destroy(dokan_mutex_t *m) { if (*m) vSemaphoreDelete(*m); }
static inline void dokan_mutex_lock(dokan_mutex_t *m)    { xSemaphoreTakeRecursive(*m, portMAX_DELAY); }
static inline void dokan_mutex_unlock(dokan_mutex_t *m)  { xSemaphoreGiveRecursive(*m); }
static inline dokan_task_t dokan_task_self(void)         { return xTaskGetCurrentTaskHandle(); }
static inline bool dokan_task_eq(dokan_task_t a, dokan_task_t b) { return a == b; }

#else
#include <pthread.h>

typedef pthread_mutex_t dokan_mutex_t;
typedef pthread_t       dokan_task_t;

static inline void dokan_mutex_init(dokan_mutex_t *m)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
}
static inline void dokan_mutex_destroy(dokan_mutex_t *m) { pthread_mutex_destroy(m); }
static inline void dokan_mutex_lock(dokan_mutex_t *m)    { pthread_mutex_lock(m); }
static inline void dokan_mutex_unlock(dokan_mutex_t *m)  { pthread_mutex_unlock(m); }
static inline dokan_task_t dokan_task_self(void)         { return pthread_self(); }
static inline bool dokan_task_eq(dokan_task_t a, dokan_task_t b) { return pthread_equal(a, b); }

#endif

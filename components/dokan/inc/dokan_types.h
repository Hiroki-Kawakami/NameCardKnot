/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Shared dokan types: roles, transport ids, sizing constants.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* enum value == the transport char in the descriptor header */
typedef enum {
    DOKAN_TRANSPORT_WIFI = 'W',
    DOKAN_TRANSPORT_BT   = 'B',
    DOKAN_TRANSPORT_SIM  = 'S',
} dokan_transport_id_t;

typedef enum {
    DOKAN_ROLE_HOST,
    DOKAN_ROLE_CLIENT,
} dokan_role_t;

#define DOKAN_APP_ID_LEN 4

/* sized to fit a narrowband out-of-band exchange channel; buffer includes NUL */
#define DOKAN_DESCRIPTOR_MAX 128

typedef struct dokan_session dokan_session_t;
typedef struct dokan_stream  dokan_stream_t;

typedef struct {
    uint16_t kind;        /* app-defined tag */
    uint64_t size_hint;   /* total bytes if known, 0 = unknown/open-ended */
} dokan_stream_opts_t;

typedef enum {
    DOKAN_EVENT_CONNECTED,
    DOKAN_EVENT_DISCONNECTED,
    DOKAN_EVENT_ERROR,            /* session fatal; err set */
    DOKAN_EVENT_STREAM_OPENED,    /* peer opened a stream; stream, opts set */
    DOKAN_EVENT_STREAM_DATA,      /* data set, valid only during the callback */
    DOKAN_EVENT_STREAM_FINISHED,  /* peer sent EOF */
    DOKAN_EVENT_STREAM_WRITABLE,  /* send window opened; write can resume */
    DOKAN_EVENT_STREAM_RESET,     /* peer aborted the stream; err set */
} dokan_event_type_t;

typedef struct {
    dokan_event_type_t  type;
    dokan_session_t    *session;
    dokan_stream_t     *stream;   /* STREAM_* */
    const uint8_t      *data;     /* STREAM_DATA */
    size_t              len;      /* STREAM_DATA */
    dokan_stream_opts_t opts;     /* STREAM_OPENED */
    esp_err_t           err;      /* ERROR / STREAM_RESET */
} dokan_event_t;

typedef void (*dokan_event_cb_t)(const dokan_event_t *ev, void *arg);

typedef struct {
    uint32_t connect_timeout_ms;  /* transport connect timeout (Phase 3); 0 = default */
    uint32_t stream_window;       /* initial per-stream window; 0 = default */
} dokan_config_t;

#ifdef __cplusplus
}
#endif

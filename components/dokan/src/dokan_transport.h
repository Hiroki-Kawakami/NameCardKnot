/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Transport seam: a transport brings up a reliable byte stream from descriptor
 * params and drives the session through the host callbacks. The session stays
 * transport-agnostic; WiFi/sim implement this vtable.
 */

#pragma once
#include "dokan_types.h"

/* Session-side callbacks a transport drives (ctx = the session). */
typedef struct {
    void (*on_connected)(void *ctx);
    void (*on_bytes)(void *ctx, const uint8_t *data, size_t len);
    void (*on_writable)(void *ctx);
    void (*on_error)(void *ctx, esp_err_t err);
    void (*on_closed)(void *ctx);     /* task exited after a deferred close */
    void (*bind_task)(void *ctx);     /* the I/O task announces itself */
    bool (*poll_close)(void *ctx);    /* true once a close was requested from a callback */
    void *ctx;
} dokan_transport_host_t;

typedef struct dokan_transport dokan_transport_t;
struct dokan_transport {
    dokan_transport_id_t id;
    esp_err_t (*start)(dokan_transport_t *self, dokan_role_t role,
                       const char app_id[DOKAN_APP_ID_LEN],
                       const uint8_t *params, size_t plen,
                       const dokan_config_t *cfg, const dokan_transport_host_t *host);
    size_t (*write)(dokan_transport_t *self, const uint8_t *data, size_t len);
    void   (*stop)(dokan_transport_t *self);  /* joins the I/O task; called cross-thread */
};

esp_err_t dokan_transport_create(dokan_transport_id_t id, dokan_transport_t **out);

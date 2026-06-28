/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * dokan_open: parse a descriptor, instantiate its transport, and bind it to a
 * session. The session then drives data exchange; events reach the app cb on the
 * transport's I/O task.
 */

#include "dokan.h"
#include "dokan_session.h"
#include "dokan_transport.h"
#include "dokan_descriptor.h"
#include <stdlib.h>

static size_t transport_write_thunk(void *ctx, const uint8_t *data, size_t len) {
    dokan_transport_t *t = ctx;
    return t->write(t, data, len);
}

esp_err_t dokan_open(const char *descriptor, dokan_role_t role,
                     const char app_id[DOKAN_APP_ID_LEN], const dokan_config_t *cfg,
                     dokan_event_cb_t cb, void *arg, dokan_session_t **out) {
    if (!descriptor || !app_id || !out) return ESP_ERR_INVALID_ARG;

    dokan_transport_id_t tid;
    uint8_t params[DOKAN_PARAMS_MAX];
    size_t plen;
    esp_err_t err = dokan_descriptor_parse(descriptor, app_id, &tid, params, sizeof params, &plen);
    if (err != ESP_OK) return err;

    dokan_transport_t *t;
    err = dokan_transport_create(tid, &t);
    if (err != ESP_OK) return err;

    dokan_session_t *s;
    err = dokan_session_create_raw(role, cfg, transport_write_thunk, t, cb, arg, &s);
    if (err != ESP_OK) { free(t); return err; }
    dokan_session_set_transport(s, t);

    dokan_transport_host_t host;
    dokan_session_make_host(s, &host);
    err = t->start(t, role, app_id, params, plen, cfg, &host);
    if (err != ESP_OK) { dokan_session_destroy(s); return err; }

    *out = s;
    return ESP_OK;
}

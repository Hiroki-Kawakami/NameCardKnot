/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Data-plane engine: frame multiplexing + flow control over a reliable byte
 * stream. Pure and single-threaded; Phase 3 wraps it with a task, a lock, and a
 * real transport.
 */

#pragma once
#include "dokan_types.h"
#include "dokan_transport.h"

/* Non-blocking transport write -> bytes accepted (may be < len). The unsent
 * remainder is retried on dokan_session_on_writable. */
typedef size_t (*dokan_transport_write_fn)(void *ctx, const uint8_t *data, size_t len);

/* Build a session over an already-connected transport (the handshake and the
 * CONNECTED event are the caller's job). */
esp_err_t dokan_session_create_raw(dokan_role_t role, const dokan_config_t *cfg,
                                   dokan_transport_write_fn write_fn, void *write_ctx,
                                   dokan_event_cb_t event_cb, void *event_arg,
                                   dokan_session_t **out);

/* Feed bytes received from the peer (no lock; the caller serializes). */
void dokan_session_feed(dokan_session_t *s, const uint8_t *data, size_t len);
void dokan_session_on_writable(dokan_session_t *s);

/* Runtime wiring (dokan_open / transports). */
void dokan_session_set_transport(dokan_session_t *s, dokan_transport_t *t);
void dokan_session_make_host(dokan_session_t *s, dokan_transport_host_t *host);
void dokan_session_destroy(dokan_session_t *s);

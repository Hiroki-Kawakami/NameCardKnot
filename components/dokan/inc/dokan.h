/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Public dokan API: establish a P2P link from a connection descriptor exchanged
 * over a short-range channel. Spec: docs/dokan.md.
 */

#pragma once
#include "dokan_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mint a fresh descriptor into out (>= DOKAN_DESCRIPTOR_MAX bytes). Stateless. */
esp_err_t dokan_descriptor_create(dokan_transport_id_t transport,
                                  const char app_id[DOKAN_APP_ID_LEN],
                                  char *out, size_t cap);

bool dokan_descriptor_valid(const char *descriptor,
                            const char app_id[DOKAN_APP_ID_LEN]);

/* Stream multiplexing over an established session; events arrive on the cb given
 * at session creation. Streams are unidirectional: the opener writes, the peer
 * reads. Spec: docs/dokan.md. */
esp_err_t dokan_close(dokan_session_t *s);

esp_err_t dokan_stream_open(dokan_session_t *s, const dokan_stream_opts_t *opts,
                            dokan_stream_t **out);
/* Non-blocking: accepts up to the send window; *written < len means the window
 * is full — wait for DOKAN_EVENT_STREAM_WRITABLE. opener only. */
esp_err_t dokan_stream_write(dokan_stream_t *st, const void *data, size_t len, size_t *written);
esp_err_t dokan_stream_finish(dokan_stream_t *st);                  /* opener only; send EOF */
esp_err_t dokan_stream_reset(dokan_stream_t *st, esp_err_t reason); /* either end */
esp_err_t dokan_stream_pause(dokan_stream_t *st);                   /* reader only */
esp_err_t dokan_stream_resume(dokan_stream_t *st);                  /* reader only */
void      dokan_stream_release(dokan_stream_t *st);
void      dokan_stream_set_user(dokan_stream_t *st, void *user);
void     *dokan_stream_get_user(dokan_stream_t *st);

#ifdef __cplusplus
}
#endif

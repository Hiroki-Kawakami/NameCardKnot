/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Descriptor wire format and codec (transport-agnostic): "DK" | '1' | app_id[4]
 * | transport char | base64url(params || crc8). The params bytes are opaque
 * here; each transport defines and decodes its own.
 */

#pragma once
#include "dokan_types.h"

#define DOKAN_DESC_HEADER_LEN 8
#define DOKAN_PARAMS_MAX      48   /* max raw transport params (pre base64url) */

esp_err_t dokan_descriptor_encode(dokan_transport_id_t transport,
                                  const char app_id[DOKAN_APP_ID_LEN],
                                  const uint8_t *params, size_t plen,
                                  char *out, size_t cap);

esp_err_t dokan_descriptor_parse(const char *descriptor,
                                 const char app_id[DOKAN_APP_ID_LEN],
                                 dokan_transport_id_t *transport_out,
                                 uint8_t *params_out, size_t cap, size_t *plen_out);

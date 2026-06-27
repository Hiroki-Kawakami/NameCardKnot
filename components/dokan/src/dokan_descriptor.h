/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Descriptor wire format and codec: "DK" | '1' | app_id[4] | transport char |
 * base64url(params||crc8). encode/parse for the transport runtime.
 */

#pragma once
#include "dokan_types.h"
#include "dokan_wifi_params.h"

#define DOKAN_DESC_HEADER_LEN 8

esp_err_t dokan_descriptor_encode(dokan_transport_id_t transport,
                                  const char app_id[DOKAN_APP_ID_LEN],
                                  const dokan_wifi_params_t *wifi,
                                  char *out, size_t cap);

esp_err_t dokan_descriptor_parse(const char *descriptor,
                                 const char app_id[DOKAN_APP_ID_LEN],
                                 dokan_transport_id_t *transport_out,
                                 dokan_wifi_params_t *wifi_out);

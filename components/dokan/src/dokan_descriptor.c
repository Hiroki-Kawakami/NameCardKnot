/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan.h"
#include "dokan_descriptor.h"
#include "dokan_codec.h"
#include "dokan_wifi_params.h"
#include "dokan_random.h"
#include <string.h>

#define DOKAN_WIFI_DEFAULT_CHANNEL 6
#define DOKAN_WIFI_DEFAULT_PORT    3939

static bool app_id_printable(const char app_id[DOKAN_APP_ID_LEN]) {
    for (int i = 0; i < DOKAN_APP_ID_LEN; i++)
        if (app_id[i] < 0x21 || app_id[i] > 0x7e) return false;
    return true;
}

esp_err_t dokan_descriptor_encode(dokan_transport_id_t transport,
                                  const char app_id[DOKAN_APP_ID_LEN],
                                  const uint8_t *params, size_t plen,
                                  char *out, size_t cap) {
    if (!app_id || !params || !out) return ESP_ERR_INVALID_ARG;
    if (!app_id_printable(app_id)) return ESP_ERR_INVALID_ARG;
    if (plen == 0 || plen > DOKAN_PARAMS_MAX) return ESP_ERR_INVALID_SIZE;

    uint8_t body[DOKAN_PARAMS_MAX + 1];
    memcpy(body, params, plen);
    body[plen] = dokan_crc8(params, plen);

    char b64[((DOKAN_PARAMS_MAX + 1 + 2) / 3) * 4 + 1];
    size_t n = dokan_base64url_encode(body, plen + 1, b64, sizeof b64);
    if (n == 0) return ESP_FAIL;

    size_t total = DOKAN_DESC_HEADER_LEN + n;
    if (cap < total + 1) return ESP_ERR_INVALID_SIZE;

    out[0] = 'D'; out[1] = 'K'; out[2] = '1';
    memcpy(out + 3, app_id, DOKAN_APP_ID_LEN);
    out[7] = (char)transport;
    memcpy(out + DOKAN_DESC_HEADER_LEN, b64, n);
    out[total] = '\0';
    return ESP_OK;
}

esp_err_t dokan_descriptor_parse(const char *descriptor,
                                 const char app_id[DOKAN_APP_ID_LEN],
                                 dokan_transport_id_t *transport_out,
                                 uint8_t *params_out, size_t cap, size_t *plen_out) {
    if (!descriptor || !app_id || !params_out || !plen_out) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(descriptor);
    if (len < DOKAN_DESC_HEADER_LEN || len > DOKAN_DESCRIPTOR_MAX) return ESP_ERR_INVALID_SIZE;
    if (descriptor[0] != 'D' || descriptor[1] != 'K') return ESP_ERR_INVALID_VERSION;
    if (descriptor[2] != '1') return ESP_ERR_INVALID_VERSION;
    if (memcmp(descriptor + 3, app_id, DOKAN_APP_ID_LEN) != 0) return ESP_ERR_INVALID_ARG;

    uint8_t body[DOKAN_PARAMS_MAX + 1];
    size_t bodylen = 0;
    if (dokan_base64url_decode(descriptor + DOKAN_DESC_HEADER_LEN, len - DOKAN_DESC_HEADER_LEN,
                               body, sizeof body, &bodylen) != 0)
        return ESP_ERR_INVALID_ARG;
    if (bodylen < 1) return ESP_ERR_INVALID_SIZE;

    size_t plen = bodylen - 1;
    if (dokan_crc8(body, plen) != body[plen]) return ESP_ERR_INVALID_CRC;
    if (plen > cap) return ESP_ERR_INVALID_SIZE;

    memcpy(params_out, body, plen);
    *plen_out = plen;
    if (transport_out) *transport_out = (dokan_transport_id_t)descriptor[7];
    return ESP_OK;
}

esp_err_t dokan_descriptor_create(dokan_transport_id_t transport,
                                  const char app_id[DOKAN_APP_ID_LEN],
                                  char *out, size_t cap) {
    if (transport != DOKAN_TRANSPORT_WIFI) return ESP_ERR_NOT_SUPPORTED;

    dokan_wifi_params_t p = {0};
    p.channel = DOKAN_WIFI_DEFAULT_CHANNEL;
    p.port = DOKAN_WIFI_DEFAULT_PORT;
    esp_err_t err = dokan_random_fill(p.seed, sizeof p.seed);
    if (err != ESP_OK) return err;

    uint8_t params[DOKAN_WIFI_PARAMS_BIN_LEN];
    dokan_wifi_params_encode(&p, params);
    return dokan_descriptor_encode(transport, app_id, params, sizeof params, out, cap);
}

bool dokan_descriptor_valid(const char *descriptor, const char app_id[DOKAN_APP_ID_LEN]) {
    dokan_transport_id_t t;
    uint8_t params[DOKAN_PARAMS_MAX];
    size_t plen;
    return dokan_descriptor_parse(descriptor, app_id, &t, params, sizeof params, &plen) == ESP_OK;
}

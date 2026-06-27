/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan.h"
#include "dokan_descriptor.h"
#include "dokan_codec.h"
#include "dokan_random.h"
#include <string.h>

#define DOKAN_WIFI_DEFAULT_CHANNEL 6
#define DOKAN_WIFI_DEFAULT_PORT    3939

/* body = params(20) + crc8(1) = 21 bytes -> 28 base64url chars */
#define DOKAN_WIFI_BODY_BIN_LEN (DOKAN_WIFI_PARAMS_BIN_LEN + 1)

static bool app_id_printable(const char app_id[DOKAN_APP_ID_LEN]) {
    for (int i = 0; i < DOKAN_APP_ID_LEN; i++)
        if (app_id[i] < 0x21 || app_id[i] > 0x7e) return false;
    return true;
}

esp_err_t dokan_descriptor_encode(dokan_transport_id_t transport,
                                  const char app_id[DOKAN_APP_ID_LEN],
                                  const dokan_wifi_params_t *wifi,
                                  char *out, size_t cap) {
    if (!app_id || !wifi || !out) return ESP_ERR_INVALID_ARG;
    if (transport != DOKAN_TRANSPORT_WIFI) return ESP_ERR_NOT_SUPPORTED;
    if (!app_id_printable(app_id)) return ESP_ERR_INVALID_ARG;

    uint8_t body[DOKAN_WIFI_BODY_BIN_LEN];
    esp_err_t err = dokan_wifi_params_encode(wifi, body);
    if (err != ESP_OK) return err;
    body[DOKAN_WIFI_PARAMS_BIN_LEN] = dokan_crc8(body, DOKAN_WIFI_PARAMS_BIN_LEN);

    char b64[64];
    size_t n = dokan_base64url_encode(body, sizeof body, b64, sizeof b64);
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
                                 dokan_wifi_params_t *wifi_out) {
    if (!descriptor || !app_id || !wifi_out) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(descriptor);
    if (len < DOKAN_DESC_HEADER_LEN || len > DOKAN_DESCRIPTOR_MAX) return ESP_ERR_INVALID_SIZE;
    if (descriptor[0] != 'D' || descriptor[1] != 'K') return ESP_ERR_INVALID_VERSION;
    if (descriptor[2] != '1') return ESP_ERR_INVALID_VERSION;
    if (memcmp(descriptor + 3, app_id, DOKAN_APP_ID_LEN) != 0) return ESP_ERR_INVALID_ARG;
    if (descriptor[7] != (char)DOKAN_TRANSPORT_WIFI) return ESP_ERR_NOT_SUPPORTED;

    uint8_t body[64];
    size_t bodylen = 0;
    if (dokan_base64url_decode(descriptor + DOKAN_DESC_HEADER_LEN, len - DOKAN_DESC_HEADER_LEN,
                               body, sizeof body, &bodylen) != 0)
        return ESP_ERR_INVALID_ARG;
    if (bodylen != DOKAN_WIFI_BODY_BIN_LEN) return ESP_ERR_INVALID_SIZE;
    if (dokan_crc8(body, DOKAN_WIFI_PARAMS_BIN_LEN) != body[DOKAN_WIFI_PARAMS_BIN_LEN])
        return ESP_ERR_INVALID_CRC;

    esp_err_t err = dokan_wifi_params_decode(body, DOKAN_WIFI_PARAMS_BIN_LEN, wifi_out);
    if (err != ESP_OK) return err;
    if (transport_out) *transport_out = DOKAN_TRANSPORT_WIFI;
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

    return dokan_descriptor_encode(transport, app_id, &p, out, cap);
}

bool dokan_descriptor_valid(const char *descriptor, const char app_id[DOKAN_APP_ID_LEN]) {
    dokan_transport_id_t t;
    dokan_wifi_params_t p;
    return dokan_descriptor_parse(descriptor, app_id, &t, &p) == ESP_OK;
}

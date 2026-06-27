/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan_wifi_params.h"
#include "dokan_codec.h"
#include "dokan_sha256.h"
#include <string.h>

esp_err_t dokan_wifi_params_encode(const dokan_wifi_params_t *p,
                                   uint8_t out[DOKAN_WIFI_PARAMS_BIN_LEN]) {
    if (!p || !out) return ESP_ERR_INVALID_ARG;
    out[0] = p->flags;
    out[1] = p->channel;
    memcpy(out + 2, p->seed, DOKAN_WIFI_SEED_LEN);
    out[18] = (uint8_t)(p->port >> 8);
    out[19] = (uint8_t)(p->port & 0xff);
    return ESP_OK;
}

esp_err_t dokan_wifi_params_decode(const uint8_t *bin, size_t len, dokan_wifi_params_t *out) {
    if (!bin || !out) return ESP_ERR_INVALID_ARG;
    if (len < DOKAN_WIFI_PARAMS_BIN_LEN) return ESP_ERR_INVALID_SIZE;
    out->flags = bin[0];
    out->channel = bin[1];
    memcpy(out->seed, bin + 2, DOKAN_WIFI_SEED_LEN);
    out->port = ((uint16_t)bin[18] << 8) | bin[19];
    if (out->channel < 1 || out->channel > 13) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

void dokan_wifi_derive(const char app_id[DOKAN_APP_ID_LEN],
                       const uint8_t seed[DOKAN_WIFI_SEED_LEN],
                       char ssid[DOKAN_WIFI_SSID_MAX], char psk[DOKAN_WIFI_PSK_MAX]) {
    uint8_t in[DOKAN_APP_ID_LEN + DOKAN_WIFI_SEED_LEN];
    memcpy(in, app_id, DOKAN_APP_ID_LEN);
    memcpy(in + DOKAN_APP_ID_LEN, seed, DOKAN_WIFI_SEED_LEN);

    uint8_t key[32];
    dokan_sha256(in, sizeof in, key);

    memcpy(ssid, "dokan-", 6);
    size_t n = dokan_base32_encode(key, 4, ssid + 6, DOKAN_WIFI_SSID_MAX - 7);
    ssid[6 + n] = '\0';

    size_t m = dokan_base32_encode(key + 4, 12, psk, DOKAN_WIFI_PSK_MAX - 1);
    psk[m] = '\0';
}

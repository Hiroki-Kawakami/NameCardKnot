/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * WiFi P2P descriptor params + the SSID/PSK derivation (KDF).
 */

#pragma once
#include "dokan_types.h"

#define DOKAN_WIFI_SEED_LEN       16
#define DOKAN_WIFI_PARAMS_BIN_LEN 20

/* wire layout (big-endian): flags:1 | channel:1 | seed:16 | port:2 = 20 bytes
 * (crc8 added by the descriptor layer) */
typedef struct {
    uint8_t  flags;                      /* reserved */
    uint8_t  channel;                    /* 1..13 */
    uint8_t  seed[DOKAN_WIFI_SEED_LEN];  /* shared secret; derives SSID/PSK */
    uint16_t port;
} dokan_wifi_params_t;

esp_err_t dokan_wifi_params_encode(const dokan_wifi_params_t *p,
                                   uint8_t out[DOKAN_WIFI_PARAMS_BIN_LEN]);
esp_err_t dokan_wifi_params_decode(const uint8_t *bin, size_t len,
                                   dokan_wifi_params_t *out);

/* Derive SSID/PSK deterministically from app_id+seed (both ends must agree). */
#define DOKAN_WIFI_SSID_MAX 32
#define DOKAN_WIFI_PSK_MAX  32
void dokan_wifi_derive(const char app_id[DOKAN_APP_ID_LEN],
                       const uint8_t seed[DOKAN_WIFI_SEED_LEN],
                       char ssid[DOKAN_WIFI_SSID_MAX], char psk[DOKAN_WIFI_PSK_MAX]);

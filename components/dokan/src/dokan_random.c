/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan_random.h"

#ifdef ESP_PLATFORM
#include "esp_random.h"

esp_err_t dokan_random_fill(void *buf, size_t len) {
    esp_fill_random(buf, len);
    return ESP_OK;
}

#else
#include <stdio.h>

esp_err_t dokan_random_fill(void *buf, size_t len) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return ESP_FAIL;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n == len ? ESP_OK : ESP_FAIL;
}
#endif

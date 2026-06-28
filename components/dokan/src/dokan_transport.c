/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan_transport.h"

#ifdef ESP_PLATFORM
esp_err_t dokan_wifi_transport_create(dokan_transport_t **out);
#else
esp_err_t dokan_sim_transport_create(dokan_transport_t **out);
#endif

esp_err_t dokan_transport_create(dokan_transport_id_t id, dokan_transport_t **out) {
    switch (id) {
#ifdef ESP_PLATFORM
    case DOKAN_TRANSPORT_WIFI:
        return dokan_wifi_transport_create(out);
#else
    case DOKAN_TRANSPORT_SIM:
        return dokan_sim_transport_create(out);
#endif
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

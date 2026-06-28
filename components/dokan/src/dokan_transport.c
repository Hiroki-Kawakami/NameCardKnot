/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan_transport.h"

#ifndef ESP_PLATFORM
esp_err_t dokan_sim_transport_create(dokan_transport_t **out);
#endif

esp_err_t dokan_transport_create(dokan_transport_id_t id, dokan_transport_t **out) {
    switch (id) {
    case DOKAN_TRANSPORT_SIM:
#ifndef ESP_PLATFORM
        return dokan_sim_transport_create(out);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    default:
        return ESP_ERR_NOT_SUPPORTED;  /* WiFi transport arrives in a later phase */
    }
}

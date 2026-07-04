/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "BootMessage.hpp"
#include "nvs_flash.h"
#include "nvs.h"

namespace bootmsg {

static const char *kNs = "bootmsg";

static bool open_store(nvs_open_mode_t mode, nvs_handle_t *out) {
    static bool inited = false;
    if (!inited) {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        if (err != ESP_OK) return false;
        inited = true;
    }
    return nvs_open(kNs, mode, out) == ESP_OK;
}

void save(Id id, const std::string &param) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_set_u32(h, "id", (uint32_t)id);
    nvs_set_str(h, "param", param.c_str());
    nvs_commit(h);
    nvs_close(h);
}

Info take() {
    Info info;
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return info;

    uint32_t id = 0;
    nvs_get_u32(h, "id", &id);
    info.id = (Id)id;

    size_t len = 0;
    if (nvs_get_str(h, "param", nullptr, &len) == ESP_OK && len > 1) {
        info.param.resize(len);
        if (nvs_get_str(h, "param", info.param.data(), &len) == ESP_OK) info.param.resize(len - 1);
        else info.param.clear();
    }

    nvs_erase_key(h, "id");
    nvs_erase_key(h, "param");
    nvs_commit(h);
    nvs_close(h);
    return info;
}

void clear() {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_erase_key(h, "id");
    nvs_erase_key(h, "param");
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace bootmsg

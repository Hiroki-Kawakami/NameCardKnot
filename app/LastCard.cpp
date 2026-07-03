/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "LastCard.hpp"
#include "nvs_flash.h"
#include "nvs.h"

namespace lastcard {

static const char *kNs = "lastcard";

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

Info load() {
    Info info;
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return info;
    uint8_t src = 0;
    nvs_get_u8(h, "src", &src);
    if (src == (uint8_t)Source::MyCard) {
        info.source = Source::MyCard;
    } else if (src == (uint8_t)Source::SdFile) {
        size_t len = 0;
        if (nvs_get_str(h, "path", nullptr, &len) == ESP_OK && len > 1) {
            info.path.resize(len);
            if (nvs_get_str(h, "path", info.path.data(), &len) == ESP_OK) {
                info.path.resize(len - 1);  // drop the NUL
                info.source = Source::SdFile;
            } else {
                info.path.clear();
            }
        }
    }
    nvs_close(h);
    return info;
}

void save_mycard() {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_set_u8(h, "src", (uint8_t)Source::MyCard);
    nvs_erase_key(h, "path");
    nvs_commit(h);
    nvs_close(h);
}

void save_sd_file(const std::string &path) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_set_u8(h, "src", (uint8_t)Source::SdFile);
    nvs_set_str(h, "path", path.c_str());
    nvs_commit(h);
    nvs_close(h);
}

void clear() {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace lastcard

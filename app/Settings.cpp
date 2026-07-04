/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "Settings.hpp"
#include "nvs_flash.h"
#include "nvs.h"

namespace settings {

static const char *kNs = "ncksettings";

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

bool get_bool(const char *key, bool def) {
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return def;
    uint8_t v = def ? 1 : 0;
    nvs_get_u8(h, key, &v);
    nvs_close(h);
    return v != 0;
}

void set_bool(const char *key, bool value) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_set_u8(h, key, value ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

uint32_t get_u32(const char *key, uint32_t def) {
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return def;
    uint32_t v = def;
    nvs_get_u32(h, key, &v);
    nvs_close(h);
    return v;
}

void set_u32(const char *key, uint32_t value) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_set_u32(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

std::string get_str(const char *key, const std::string &def) {
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return def;
    std::string out = def;
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 1) {
        out.resize(len);
        if (nvs_get_str(h, key, out.data(), &len) == ESP_OK) out.resize(len - 1);
        else out = def;
    }
    nvs_close(h);
    return out;
}

void set_str(const char *key, const std::string &value) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    nvs_set_str(h, key, value.c_str());
    nvs_commit(h);
    nvs_close(h);
}

bool share_receive_return()            { return get_bool("share_ret", false); }
void set_share_receive_return(bool v)  { set_bool("share_ret", v); }
bool receive_send_return()             { return get_bool("recv_ret", false); }
void set_receive_send_return(bool v)   { set_bool("recv_ret", v); }

}  // namespace settings

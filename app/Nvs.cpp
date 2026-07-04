/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "Nvs.hpp"
#include "CardStore.hpp"
#include "NameCardData.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstdio>
#include <cstring>
#include <vector>

// Internal typed key/value store over one NVS namespace. Each call opens/commits/
// closes its own handle; setters return whether the commit succeeded.
namespace {

const char *kNs = "namecardknot";

bool open_store(nvs_open_mode_t mode, nvs_handle_t *out) {
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

bool commit_close(nvs_handle_t h, esp_err_t err) {
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool get_bool(const char *key, bool def) {
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return def;
    uint8_t v = def ? 1 : 0;
    nvs_get_u8(h, key, &v);
    nvs_close(h);
    return v != 0;
}

bool set_bool(const char *key, bool value) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return false;
    return commit_close(h, nvs_set_u8(h, key, value ? 1 : 0));
}

uint32_t get_u32(const char *key, uint32_t def) {
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return def;
    uint32_t v = def;
    nvs_get_u32(h, key, &v);
    nvs_close(h);
    return v;
}

bool set_u32(const char *key, uint32_t value) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return false;
    return commit_close(h, nvs_set_u32(h, key, value));
}

std::string get_str(const char *key, const std::string &def) {
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return def;
    std::string out = def;
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 1) {
        out.resize(len);
        if (nvs_get_str(h, key, out.data(), &len) == ESP_OK) out.resize(len - 1);  // drop the NUL
        else out = def;
    }
    nvs_close(h);
    return out;
}

bool set_str(const char *key, const std::string &value) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return false;
    return commit_close(h, nvs_set_str(h, key, value.c_str()));
}

bool erase(const char *key) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return false;
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    return commit_close(h, err);
}

}  // namespace

namespace bootmsg {

void save(Id id, const std::string &param) {
    set_u32("boot_id", (uint32_t)id);
    set_str("boot_param", param);
}

Info take() {
    Info info;
    info.id = (Id)get_u32("boot_id", 0);
    info.param = get_str("boot_param", "");
    clear();
    return info;
}

void clear() {
    erase("boot_id");
    erase("boot_param");
}

}  // namespace bootmsg

namespace lastcard {

static const char *TAG = "lastcard";

Info load() {
    Info info;
    uint32_t src = get_u32("src", 0);
    if (src == (uint32_t)Source::MyCard) {
        info.source = Source::MyCard;
    } else if (src == (uint32_t)Source::SdFile) {
        info.path = get_str("path", "");
        if (!info.path.empty()) info.source = Source::SdFile;
    }
    return info;
}

void save_mycard() {
    set_u32("src", (uint32_t)Source::MyCard);
    erase("path");
}

void save_sd_file(const std::string &path) {
    set_u32("src", (uint32_t)Source::SdFile);
    set_str("path", path);
}

void clear() {
    erase("src");   // keep cpath: the flash cache stays valid
    erase("path");
}

void save_received(const std::string &path) {
    set_str("rcvpath", path);
}

std::string take_received() {
    std::string path = get_str("rcvpath", "");
    erase("rcvpath");
    return path;
}

std::string cache_path() {
    return get_str("cpath", "");
}

static void set_cache_path(const char *path) {
    if (path) set_str("cpath", path);
    else      erase("cpath");
}

static bool s_clean_known, s_clean;   // RAM cache; all callers run on one task at a time

void set_clean_resume(bool clean) {
    if (s_clean_known && s_clean == clean) return;
    if (!set_bool("clean", clean)) {
        // Keep the cache stale so the next call retries instead of short-circuiting.
        ESP_LOGE(TAG, "set_clean_resume(%d): write failed", clean);
        return;
    }
    s_clean_known = true;
    s_clean = clean;
}

bool clean_resume() {
    if (!s_clean_known) {
        s_clean = get_bool("clean", false);
        s_clean_known = true;
    }
    return s_clean;
}

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (size > 0) {
        out.resize((size_t)size);
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;
}

void save_cache(const std::shared_ptr<NameCardData> &data) {
    if (!data || data->state() != NameCardData::State::Ok) return;
    const std::string &path = data->path();
    if (path.empty()) return;   // My Card: already in its own partition

    auto &st = cardstore::lastcard();
    if (!st.mount()) return;
    if (st.available() && cache_path() == path && st.blob_len(cardstore::BLOB_DISPLAY))
        return;   // this card is fully cached already

    set_cache_path(nullptr);   // partition about to be rewritten
    cardstore::Store::Writer w(st);
    if (!w.begin()) return;

    if (data->is_card()) {   // the source PDF: Info/Share metadata + decode fallback
        std::vector<uint8_t> pdf;
        if (!read_file(path, pdf) ||
            !w.write_blob(cardstore::BLOB_PDF, pdf.data(), (uint32_t)pdf.size()) ||
            !w.commit()) {
            w.abort();
            return;
        }
        set_cache_path(path.c_str());   // PDF alone can already restore
    }

    const L8View &v = data->display_view();
    if (!v.valid()) return;
    std::vector<uint8_t> packed;   // store contiguous rows (stride == width)
    const uint8_t *px = v.data;
    if (v.stride != v.w) {
        packed.resize((size_t)v.w * v.h);
        for (uint32_t y = 0; y < v.h; y++)
            memcpy(packed.data() + (size_t)y * v.w, v.data + (size_t)y * v.stride, v.w);
        px = packed.data();
    }
    cardstore::Blob meta{};
    meta.w = v.w;
    meta.h = v.h;
    meta.stride = v.w;
    meta.levels = v.levels;
    meta.format = cardstore::FMT_L8;
    if (w.write_blob(cardstore::BLOB_DISPLAY, px, (uint32_t)v.w * v.h, &meta) && w.commit()) {
        if (!data->is_card()) set_cache_path(path.c_str());
    }
}

}  // namespace lastcard

namespace settings {

bool share_receive_return()            { return get_bool("share_ret", false); }
void set_share_receive_return(bool v)  { set_bool("share_ret", v); }
bool receive_send_return()             { return get_bool("recv_ret", false); }
void set_receive_send_return(bool v)   { set_bool("recv_ret", v); }

}  // namespace settings

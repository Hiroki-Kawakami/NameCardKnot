/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "LastCard.hpp"
#include "CardStore.hpp"
#include "NameCardData.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace lastcard {

static const char *TAG = "lastcard";
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
    nvs_erase_key(h, "src");   // keep cpath: the flash cache stays valid
    nvs_erase_key(h, "path");
    nvs_commit(h);
    nvs_close(h);
}

std::string cache_path() {
    std::string path;
    nvs_handle_t h;
    if (!open_store(NVS_READONLY, &h)) return path;
    size_t len = 0;
    if (nvs_get_str(h, "cpath", nullptr, &len) == ESP_OK && len > 1) {
        path.resize(len);
        if (nvs_get_str(h, "cpath", path.data(), &len) == ESP_OK) path.resize(len - 1);
        else path.clear();
    }
    nvs_close(h);
    return path;
}

static void set_cache_path(const char *path) {
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) return;
    if (path) nvs_set_str(h, "cpath", path);
    else      nvs_erase_key(h, "cpath");
    nvs_commit(h);
    nvs_close(h);
}

static bool s_clean_known, s_clean;   // RAM cache; all callers run on one task at a time

void set_clean(bool clean) {
    if (s_clean_known && s_clean == clean) return;
    nvs_handle_t h;
    if (!open_store(NVS_READWRITE, &h)) {
        ESP_LOGE(TAG, "set_clean(%d): open failed", clean);
        return;
    }
    esp_err_t err = clean ? nvs_set_u8(h, "clean", 1) : nvs_erase_key(h, "clean");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;   // erasing an absent key
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        // Keep the cache stale so the next call retries instead of short-circuiting.
        ESP_LOGE(TAG, "set_clean(%d): %s", clean, esp_err_to_name(err));
        return;
    }
    s_clean_known = true;
    s_clean = clean;
}

bool clean() {
    if (!s_clean_known) {
        uint8_t v = 0;
        nvs_handle_t h;
        if (!open_store(NVS_READONLY, &h)) return false;
        nvs_get_u8(h, "clean", &v);
        nvs_close(h);
        s_clean_known = true;
        s_clean = v != 0;
    }
    return s_clean;
}

void invalidate_clean() {
    if (clean()) set_clean(false);
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

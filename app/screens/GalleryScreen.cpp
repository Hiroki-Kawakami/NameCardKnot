/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "GalleryScreen.hpp"
#include "NameCardKnot.hpp"
#include "SharedCardData.hpp"
#include "SharedCardScreen.hpp"
#include "resources.h"
#include "Strings.hpp"
#include "UiFont.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

void GalleryScreen::build() {
    createNavigation(S().gallery);
    if (!mount_sd_card()) {
        showSdError();
        return;
    }
    load();
    rebuild();
}

void GalleryScreen::back() {
    screen_manager.pop();
}

void GalleryScreen::showSdError() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
    auto card = lv_modal_open(root_);
    lv_modal_title_create(card, S().error);
    lv_modal_message_create(card, S().sd_card_not_found);
    lv_modal_button_create(card, S().close, LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t*) {
        lv_async_call([this] {
            epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
            back();
        });
    });
}

static bool ends_with_snc_pdf(const std::string &s) {
    static constexpr char suffix[] = ".snc.pdf";
    size_t n = sizeof(suffix) - 1;
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

void GalleryScreen::load() {
    entries_.clear();
    offset_ = 0;

    DIR *dir = opendir(RECEIVED_CARDS_DIR);
    if (!dir) return;  // no ReceivedCards dir yet: empty, not an error
    while (auto *ent = readdir(dir)) {
        if (ent->d_name[0] == '.') continue;
        std::string name = ent->d_name;
        if (!ends_with_snc_pdf(name)) continue;
        std::string full = std::string(RECEIVED_CARDS_DIR) + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        entries_.push_back(Entry{name, st.st_mtime});
    }
    closedir(dir);

    std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;  // newest first
        const std::string &x = a.name, &y = b.name;  // case-insensitive name compare
        for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
            int cx = std::tolower((unsigned char)x[i]);
            int cy = std::tolower((unsigned char)y[i]);
            if (cx != cy) return cx < cy;
        }
        return x.size() < y.size();
    });
}

void GalleryScreen::rebuild() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    lv_obj_clean(contents_);
    row_names_.clear();

    if (entries_.empty()) {
        auto label = lv_label_create(contents_);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, S().no_items);
        lv_obj_set_style_text_font(label, ui_font_24(), 0);
        lv_obj_set_style_margin_top(label, 40, 0);
        return;
    }

    constexpr size_t page_item_num = 8;
    int page_num = (entries_.size() + page_item_num - 1) / page_item_num;
    int page = offset_ / page_item_num;
    bool has_prev_page = page > 0;
    bool has_next_page = page + 1 < page_num;

    for (size_t i = 0; i < page_item_num; i++) {
        if (i > 0) lv_hor_separator_create(contents_);
        if (offset_ + i >= entries_.size()) {
            lv_spacer_create(contents_, LV_PCT(100), 0, 1);
            break;
        }

        auto &e = entries_[offset_ + i];
        auto row = lv_button_create(contents_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 96);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_row(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_color(row, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_set_user_data(row, (void*)(offset_ + i));
        lv_obj_add_event_cb(row, [](lv_event_t *ev) {
            auto self = (GalleryScreen*)lv_event_get_user_data(ev);
            auto button = lv_event_get_target_obj(ev);
            auto index = (size_t)lv_obj_get_user_data(button);
            self->open(index);
        }, LV_EVENT_CLICKED, this);

        std::string full = std::string(RECEIVED_CARDS_DIR) + "/" + e.name;
        auto name_label = std::make_unique<CardNameLabel>();
        lv_obj_t *name_widget = lv_label_create(row);
        if (name_label->load_file(full, 32)) {
            lv_label_set_text(name_widget, name_label->text().c_str());
            lv_obj_set_style_text_font(name_widget, name_label->font(), 0);
            row_names_.push_back(std::move(name_label));
        } else {
            lv_label_set_text(name_widget, e.name.c_str());
            lv_obj_set_style_text_font(name_widget, ui_font_24(), 0);
        }
        lv_obj_set_width(name_widget, LV_PCT(100));
        lv_label_set_long_mode(name_widget, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name_widget, LV_TEXT_ALIGN_LEFT, 0);

        struct tm tmv;
        localtime_r(&e.mtime, &tmv);
        char buf[64];  // generous: tm fields are plain int, so -Wformat-truncation assumes worst case
        snprintf(buf, sizeof buf, "%04d/%02d/%02d %02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        auto date_widget = lv_label_create(row);
        lv_label_set_text(date_widget, buf);
        lv_obj_set_style_text_font(date_widget, ui_font_24(), 0);
    }

    auto status = lv_container_create(contents_);
    lv_obj_set_size(status, LV_PCT(100), 81);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(status, 2, 0);
    lv_obj_set_style_border_color(status, lv_color_black(), 0);

    auto label = lv_label_create(status);
    char status_buf[64];
    snprintf(status_buf, sizeof status_buf, S().items_page_fmt, (int)entries_.size(), page + 1, page_num);
    lv_label_set_text(label, status_buf);
    lv_obj_set_style_text_font(label, ui_font_24(), 0);
    lv_obj_center(label);

    if (has_prev_page) {
        auto button = lv_button_create(status);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 60, 60);
        lv_obj_align(button, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_border_color(button, lv_color_white(), 0);
        lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [this](lv_event_t*) {
            offset_ -= page_item_num;
            rebuild();
        });

        auto icon = lv_label_create(button);
        lv_obj_center(icon);
        lv_label_set_text(icon, LUCIDE_CIRCLE_ARROW_LEFT);
        lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);
    }
    if (has_next_page) {
        auto button = lv_button_create(status);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 60, 60);
        lv_obj_align(button, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_border_color(button, lv_color_white(), 0);
        lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [this](lv_event_t*) {
            offset_ += page_item_num;
            rebuild();
        });

        auto icon = lv_label_create(button);
        lv_obj_center(icon);
        lv_label_set_text(icon, LUCIDE_CIRCLE_ARROW_RIGHT);
        lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);
    }
}

void GalleryScreen::open(int index) {
    if (index < 0 || (size_t)index >= entries_.size()) return;
    std::string full = std::string(RECEIVED_CARDS_DIR) + "/" + entries_[index].name;
    auto data = SharedCardData::open(full);
    if (!data || !data->valid()) return;
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
    screen_manager.push(std::make_shared<SharedCardScreen>(data, SharedCardScreen::Nav::Back));
}

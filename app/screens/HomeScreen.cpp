/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "HomeScreen.hpp"
#include "widgets.hpp"
#include "resources.h"
#include "NameCardKnot.hpp"
#include "FileBrowserScreen.hpp"
#include "GalleryScreen.hpp"
#include "NameCardScreen.hpp"
#include "NameCardData.hpp"
#include "CardStore.hpp"
#include "lv_image_adapter.hpp"
#include "ShareScreen.hpp"
#include "ReceiveScreen.hpp"
#include "SettingsScreen.hpp"
#include "Strings.hpp"
#include "UiFont.hpp"
#include <cstdio>

void HomeScreen::build() {
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(root_, 20, 0);

    date_label_ = lv_label_create(root_);
    lv_obj_add_flag(date_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(date_label_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(date_label_, ui_font_24(), 0);
    refreshDate();

    battery_image_ = lv_image_create(root_);
    lv_obj_add_flag(battery_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(battery_image_, LV_ALIGN_TOP_RIGHT, -4, -4);
    lv_obj_set_style_image_recolor(battery_image_, lv_color_black(), 0);
    lv_obj_set_style_image_recolor_opa(battery_image_, LV_OPA_COVER, 0);
    refreshBattery();

    { // Title
        lv_obj_t *title = lv_label_create(root_);
        lv_label_set_text(title, S().app_name);
        lv_obj_set_style_text_font(title, ui_font_48(), 0);

        lv_obj_t *version = lv_label_create(root_);
        lv_label_set_text(version, "v0.1.0");
        lv_obj_set_style_text_font(version, ui_font_24(), 0);

        lv_obj_set_style_pad_top(title, 60, 0);
        lv_obj_set_style_pad_bottom(version, 40, 0);
    }

    { // My Card — filled from the store (refreshed on every appearance)
        mycard_section_ = lv_container_create(root_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(mycard_section_, LV_PCT(100), LV_SIZE_CONTENT);
        refreshMyCard();
    }

    { // Buttons
        auto button = [](lv_obj_t *parent, const void *icon, const char *title, std::function<void(lv_event_t*)> on_click) {
            auto button = lv_button_create(parent);
            lv_obj_set_size(button, 200, 140);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_radius(button, 8, 0);
            lv_obj_set_style_border_color(button, lv_color_white(), 0);
            lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
            lv_obj_add_event_fn(button, LV_EVENT_CLICKED, on_click);

            auto image = lv_image_create(button);
            lv_image_set_src(image, icon);
            lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
            lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);

            auto label = lv_label_create(button);
            lv_label_set_text(label, title);
            lv_obj_set_style_text_font(label, ui_font_24(), 0);
        };

        auto row1 = lv_container_create(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row1, 10, 0);
        lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row1, R.icon.card_sd_80px, S().open_from_sd, [](lv_event_t*) {
            screen_manager.push(std::make_shared<FileBrowserScreen>());
        });
        lv_ver_separator_create(row1);
        button(row1, R.icon.images_80px, S().gallery, [](lv_event_t*) {
            screen_manager.push(std::make_shared<GalleryScreen>());
        });

        lv_hor_separator_create(root_, 20);

        auto row2 = lv_container_create(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row2, 10, 0);
        lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row2, R.icon.square_arrow_right_enter_80px, S().receive, [](lv_event_t*) {
            std::shared_ptr<SharedCardData> shared = nullptr;
            if (auto card = NameCardData::load_cached()) {
                auto s = card->share();
                if (s && s->valid()) shared = s;
            }
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            screen_manager.push(std::make_shared<ReceiveScreen>(shared));
        });
        lv_ver_separator_create(row2);
        button(row2, R.icon.cog_80px, S().settings, [](lv_event_t*) {
            screen_manager.push(std::make_shared<SettingsScreen>());
        });
    }
}

void HomeScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
    refreshMyCard();  // a just-finished import may have replaced the card (new mmap)
    refreshDate();
    refreshBattery();
}

void HomeScreen::refreshDate() {
    bsp_rtc_datetime_t dt{};
    bool valid = false;
    bool ok = bsp_rtc_get_time(&dt) == ESP_OK && bsp_rtc_time_is_valid(&valid) == ESP_OK && valid;
    if (ok) {
        char buf[16];
        snprintf(buf, sizeof buf, "%04u/%02u/%02u", dt.year, dt.month, dt.day);
        lv_label_set_text(date_label_, buf);
    } else {
        lv_label_set_text(date_label_, "");
    }
}

void HomeScreen::refreshBattery() {
    uint8_t pct = 0;
    if (bsp_power_get_battery_level(&pct) != ESP_OK) {
        lv_obj_add_flag(battery_image_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const lv_image_dsc_t *icon = pct >= 80 ? R.icon.battery_full_32px
                               : pct >= 50 ? R.icon.battery_medium_32px
                               : pct >= 20 ? R.icon.battery_low_32px
                                           : R.icon.battery_32px;
    lv_image_set_src(battery_image_, icon);
    lv_obj_remove_flag(battery_image_, LV_OBJ_FLAG_HIDDEN);
}

void HomeScreen::onDisappear() {
    // Drop the preview + name font before leaving (the name label referenced the
    // font); free the RAM copy too.
    if (mycard_section_) lv_obj_clean(mycard_section_);
    preview_buf_.clear();
    preview_buf_.shrink_to_fit();
    name_.reset();
}

void HomeScreen::importMyCard() {
    if (mycard_section_) lv_obj_clean(mycard_section_);
    preview_buf_.clear();
    name_.reset();
    screen_manager.push(std::make_shared<FileBrowserScreen>("/sdcard", FileBrowserScreen::Mode::ImportMyCard));
}

void HomeScreen::refreshMyCard() {
    if (!mycard_section_) return;
    lv_obj_clean(mycard_section_);
    preview_buf_.clear();
    name_.reset();
    if (cardstore::mycard().available())
        myCardButtonCreate(mycard_section_);
    else
        noCardButtonCreate(mycard_section_);
}

bool HomeScreen::readPreview() {
    auto &st = cardstore::mycard();
    const cardstore::Header *h = st.header();
    if (!h) return false;
    const cardstore::Blob &b = h->blobs[cardstore::BLOB_PREVIEW];
    if (!b.length) return false;
    preview_buf_.resize(b.length);
    if (!st.read_blob(cardstore::BLOB_PREVIEW, preview_buf_.data(), b.length)) return false;
    return l8view_fill_lv_dsc(L8View{preview_buf_.data(), b.w, b.h, b.stride, b.levels}, preview_dsc_);
}

void HomeScreen::myCardButtonCreate(lv_obj_t *parent) {
    auto button = lv_button_create(parent);
    lv_obj_set_size(button, LV_PCT(100), 320);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [](lv_event_t*) {
        auto data = NameCardData::load_cached();
        if (!data) return;
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        screen_manager.load(std::make_shared<NameCardScreen>(data, NameCardScreen::Nav::Home));
    });

    auto image = lv_image_create(button);
    lv_obj_set_size(image, 169, 300);
    if (readPreview()) {
        lv_image_set_src(image, &preview_dsc_);
    } else {
        lv_obj_set_style_bg_color(image, lv_color_hex(0x888888), 0);
        lv_obj_set_style_bg_opa(image, LV_OPA_COVER, 0);
    }

    auto container = lv_container_create(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(container, 304, 300);
    lv_obj_set_style_pad_row(container, 10, 0);

    auto title = lv_label_create(container);
    lv_label_set_text(title, S().my_card);
    lv_obj_set_style_text_font(title, ui_font_32(), 0);
    lv_obj_set_style_pad_top(title, 20, 0);
    lv_spacer_create(container, 0, 0, 1);

    if (name_.load_mycard(32)) name_.make_label(container);

    lv_spacer_create(container, 0, 0, 1);

    lv_hor_separator_create(container, 10);
    auto row = lv_container_create(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_column(row, 10, 0);

    auto share_button = lv_button_create(row);
    lv_obj_remove_style_all(share_button);
    lv_obj_set_height(share_button, 64);
    lv_obj_set_flex_grow(share_button, 1);
    lv_obj_set_flex_flow(share_button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(share_button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(share_button, 10, 0);
    lv_obj_set_style_border_width(share_button, 2, 0);
    lv_obj_set_style_radius(share_button, 8, 0);
    lv_obj_set_style_border_color(share_button, lv_color_white(), 0);
    lv_obj_set_style_border_color(share_button, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(share_button, [](lv_event_t*) {
        auto card = NameCardData::load_cached();
        if (!card) return;
        auto shared = card->share();
        if (!shared || !shared->valid()) return;
        epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
        screen_manager.push(std::make_shared<ShareScreen>(shared));
    }, LV_EVENT_CLICKED, nullptr);

    auto share_icon = lv_label_create(share_button);
    lv_label_set_text(share_icon, LUCIDE_SQUARE_ARROW_OUT_UP_RIGHT);
    lv_obj_set_style_text_font(share_icon, R.font.lucide_40, 0);
    auto share_label = lv_label_create(share_button);
    lv_label_set_text(share_label, S().share);
    lv_obj_set_style_text_font(share_label, ui_font_32(), 0);

    lv_ver_separator_create(row);

    auto edit_button = lv_button_create(row);
    lv_obj_remove_style_all(edit_button);
    lv_obj_set_size(edit_button, 64, 64);
    lv_obj_set_style_border_width(edit_button, 2, 0);
    lv_obj_set_style_radius(edit_button, 8, 0);
    lv_obj_set_style_border_color(edit_button, lv_color_white(), 0);
    lv_obj_set_style_border_color(edit_button, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_add_event_fn(edit_button, LV_EVENT_CLICKED, [this](lv_event_t*) { importMyCard(); });

    auto edit_icon = lv_label_create(edit_button);
    lv_label_set_text(edit_icon, LUCIDE_DOWNLOAD);
    lv_obj_set_style_text_font(edit_icon, R.font.lucide_40, 0);
    lv_obj_center(edit_icon);
}

void HomeScreen::noCardButtonCreate(lv_obj_t *parent) {
    auto button = lv_button_create(parent);
    lv_obj_set_size(button, LV_PCT(100), 320);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(button, 20, 0);
    lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [this](lv_event_t*) { importMyCard(); });

    auto title = lv_label_create(button);
    lv_label_set_text(title, S().no_my_card);
    lv_obj_set_style_text_font(title, ui_font_32(), 0);

    auto row = lv_container_create(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);

    auto edit_icon = lv_label_create(row);
    lv_label_set_text(edit_icon, LUCIDE_DOWNLOAD);
    lv_obj_set_style_text_font(edit_icon, R.font.lucide_40, 0);
    lv_obj_center(edit_icon);

    auto edit_label = lv_label_create(row);
    lv_label_set_text(edit_label, S().import_from_sd);
    lv_obj_set_style_text_font(edit_label, ui_font_24(), 0);
}

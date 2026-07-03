/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameCardScreen.hpp"
#include "NameCardKnot.hpp"
#include "HomeScreen.hpp"
#include "LastCard.hpp"
#include "Power.hpp"
#include "lv_image_adapter.hpp"
#include "widgets.hpp"
#include "resources.h"

NameCardScreen::NameCardScreen(std::shared_ptr<NameCardData> data, Nav nav)
    : data_(std::move(data)), nav_(nav) {}

NameCardScreen::~NameCardScreen() {
    if (poll_) lv_timer_delete(poll_);
}

void NameCardScreen::build() {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_fn(root_, LV_EVENT_CLICKED, [this](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
        openMenu();
    });

    if (data_->state() == FileLoader::State::Loading) {
        poll_ = lv_timer_create([](lv_timer_t *t) {
            static_cast<NameCardScreen *>(lv_timer_get_user_data(t))->poll();
        }, 200, this);
        return;
    }
    showImage();
}

void NameCardScreen::showImage() {
    // The image is already at display resolution (decoded for an SD load, or
    // mapped from flash for a cached card), so show it 1:1 (no LVGL scaling).
    if (l8view_fill_lv_dsc(data_->display_view(), dsc_)) {
        auto image = lv_image_create(root_);
        lv_image_set_src(image, &dsc_);
        lv_obj_center(image);
    } else {
        auto label = lv_label_create(root_);
        lv_label_set_text(label, "No image");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_center(label);
    }
}

void NameCardScreen::poll() {
    if (data_->state() == FileLoader::State::Loading) return;
    lv_timer_delete(poll_);
    poll_ = nullptr;
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
    if (data_->state() == FileLoader::State::Ok) {
        showImage();
    } else {
        lastcard::clear();
        screen_manager.load(std::make_shared<HomeScreen>());
    }
}

void NameCardScreen::onEnter() {
    power::set_card_screen(this);
}

void NameCardScreen::onExit() {
    power::set_card_screen(nullptr);
}

void NameCardScreen::onAppear() {
    // Popped back to for sleep: QUALITY diff-skips the unchanged card instead
    // of re-flashing it.
    epd_set_next_refresh_mode(power::sleeping() ? BSP_EPD_MODE_QUALITY
                                                : BSP_EPD_MODE_QUALITY_ALL);
    power::set_timeout(this, 60 * 1000);
    if (data_->path().empty()) lastcard::save_mycard();
    else                       lastcard::save_sd_file(data_->path());
}

bool NameCardScreen::closeModal() {
    if (!modal_) return false;
    lv_modal_close(modal_);
    modal_ = nullptr;
    return true;
}

void NameCardScreen::leave() {
    lastcard::clear();
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
    if (nav_ == Nav::Back) screen_manager.pop();
    else                   screen_manager.load(std::make_shared<HomeScreen>());
}

void NameCardScreen::openMenu() {
    auto card = lv_modal_open(root_);
    modal_ = card;
    lv_obj_set_style_pad_row(card, 10, 0);

    { // Buttons
        auto button = [](lv_obj_t *parent, const char *icon, const char *title, std::function<void(lv_event_t*)> on_click) {
            auto button = lv_button_create(parent);
            lv_obj_set_height(button, 100);
            lv_obj_set_flex_grow(button, 1);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_radius(button, 8, 0);
            lv_obj_set_style_border_color(button, lv_color_white(), 0);
            lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
            lv_obj_add_event_fn(button, LV_EVENT_CLICKED, on_click);

            auto image = lv_label_create(button);
            lv_label_set_text(image, icon);
            lv_obj_set_style_text_font(image, R.font.lucide_40, 0);

            auto label = lv_label_create(button);
            lv_label_set_text(label, title);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        };

        if (data_->is_card()) {
            auto row1 = lv_container_create(card, LV_FLEX_FLOW_ROW);
            lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_column(row1, 10, 0);
            lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            button(row1, LUCIDE_SQUARE_ARROW_RIGHT_ENTER, "Receive", [](lv_event_t*) {});
            lv_ver_separator_create(row1);
            button(row1, LUCIDE_SQUARE_ARROW_OUT_UP_RIGHT, "Share", [](lv_event_t*) {});
            lv_ver_separator_create(row1);
            button(row1, LUCIDE_INFO, "Info", [this](lv_event_t*) {
                epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
                lv_async_call([this]() {
                    closeModal();
                    openInfo();
                });
            });

            lv_hor_separator_create(card);
        }

        auto row2 = lv_container_create(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(row2, 10, 0);
        lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (nav_ == Nav::Back) {
            button(row2, LUCIDE_ARROW_LEFT, "Back", [this](lv_event_t*) { leave(); });
        } else {
            button(row2, LUCIDE_HOME, "Home", [this](lv_event_t*) { leave(); });
        }
        lv_ver_separator_create(row2);
        button(row2, LUCIDE_COG, "Settings", [](lv_event_t*) {});
        lv_ver_separator_create(row2);
        button(row2, LUCIDE_X, "Close", [this](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            closeModal();
        });
    }
}

const lv_font_t *NameCardScreen::nameFont() {
    if (!name_font_) name_font_ = std::make_unique<NameFont>(data_->name_glyphs());
    return name_font_->font();
}

void NameCardScreen::openInfo() {
    auto card = lv_modal_open(root_);
    modal_ = card;

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, data_->name().c_str());
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_font(name, nameFont(), 0);
    lv_obj_set_style_pad_ver(name, 10, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *qr = lv_qrcode_create(card);
    lv_obj_set_width(qr, LV_PCT(100));
    lv_qrcode_set_size(qr, 300);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, data_->url().c_str(), data_->url().size());

    lv_obj_t *url = lv_label_create(card);
    lv_label_set_text(url, data_->url().c_str());
    lv_obj_set_width(url, LV_PCT(100));
    lv_obj_set_style_pad_ver(url, 10, 0);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);

    lv_modal_button_create(card, "Close", LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        closeModal();
    });
}

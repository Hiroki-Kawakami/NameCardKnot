/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "SharedCardScreen.hpp"
#include "NameCardKnot.hpp"
#include "HomeScreen.hpp"
#include "NameCardScreen.hpp"
#include "lv_image_adapter.hpp"
#include "widgets.hpp"
#include "resources.h"

SharedCardScreen::SharedCardScreen(std::shared_ptr<SharedCardData> data, Nav nav)
    : data_(std::move(data)), nav_(nav) {}

void SharedCardScreen::build() {
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (name_.set(data_->name(), data_->name_glyphs(), 48)) {
        auto label = lv_label_create(root_);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_text(label, name_.text().c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, name_.font(), 0);
        lv_obj_set_style_pad_top(label, 20, 0);
        lv_obj_set_style_pad_hor(label, 20, 0);
    }

    image_area_ = lv_container_create(root_);
    lv_obj_set_size(image_area_, LV_PCT(100), 720);
    lv_obj_remove_flag(image_area_, LV_OBJ_FLAG_CLICKABLE);
    if (decodeImage(0)) {
        showImage(0);
    } else {
        auto label = lv_label_create(image_area_);
        lv_label_set_text(label, "Cannot load image");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_center(label);
    }

    lv_spacer_create(root_, 0, 0, 1);
    buildButtonBar();
}

void SharedCardScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
}

bool SharedCardScreen::decodeImage(int index) {
    if (decoded_[index]) return true;
    imgproc::Options o;
    o.target_w = 540;
    o.target_h = 720;
    o.fit = imgproc::Fit::Contain;
    o.levels = 16;
    if (data_->decode_share_image(index, o, images_[index]) != imgproc::Status::Ok) return false;
    decoded_[index] = true;
    return true;
}

void SharedCardScreen::showImage(int index) {
    if (!imgproc_fill_lv_dsc(images_[index], dsc_[index])) return;
    if (!image_obj_) {
        image_obj_ = lv_image_create(image_area_);
        lv_obj_center(image_obj_);
    }
    lv_image_set_src(image_obj_, &dsc_[index]);
    shown_image_ = index;
}

void SharedCardScreen::toggleImage() {
    int next = shown_image_ == 0 ? 1 : 0;
    if (!decodeImage(next)) return;
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
    showImage(next);
    if (image_toggle_label_)
        lv_label_set_text(image_toggle_label_, shown_image_ == 0 ? "Image 2" : "Image 1");
}

const lv_font_t *SharedCardScreen::messageFont() {
    if (!message_font_) message_font_ = std::make_unique<NameFont>(nullptr, 24);
    return message_font_->font();
}

void SharedCardScreen::buildButtonBar() {
    auto bar = lv_container_create(root_, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bar, 20, 0);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_black(), 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto button = [](lv_obj_t *parent, const char *icon, const char *title, std::function<void(lv_event_t*)> on_click) {
        auto btn = lv_button_create(parent);
        lv_obj_set_height(btn, 100);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_border_color(btn, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_add_event_fn(btn, LV_EVENT_CLICKED, on_click);

        auto image = lv_label_create(btn);
        lv_label_set_text(image, icon);
        lv_obj_set_style_text_font(image, R.font.lucide_40, 0);

        auto label = lv_label_create(btn);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        return label;
    };

    if (nav_ == Nav::Back) {
        button(bar, LUCIDE_ARROW_LEFT, "Back", [this](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
            screen_manager.pop();
        });
    } else {
        button(bar, LUCIDE_HOME, "Home", [this](lv_event_t*) {
            auto card = make_resumed_card_screen();
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            if (card) screen_manager.load(card);
            else      screen_manager.load(std::make_shared<HomeScreen>());
        });
    }

    if (!data_->url().empty()) {
        lv_ver_separator_create(bar);
        button(bar, LUCIDE_LINK, "URL", [this](lv_event_t*) { openUrlModal(); });
    }
    if (!data_->message().empty()) {
        lv_ver_separator_create(bar);
        button(bar, LUCIDE_MESSAGE_CIRCLE, "Message", [this](lv_event_t*) { openMessageModal(); });
    }
    if (data_->share_image_count() >= 2) {
        lv_ver_separator_create(bar);
        image_toggle_label_ = button(bar, LUCIDE_IMAGES, "Image 2", [this](lv_event_t*) { toggleImage(); });
    }
}

void SharedCardScreen::openUrlModal() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
    auto card = lv_modal_open(root_);

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

    lv_modal_button_create(card, "Close", LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t*) {
        lv_async_call([card]() {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
            lv_modal_close(card);
        });
    });
}

void SharedCardScreen::openMessageModal() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
    auto card = lv_modal_open(root_);
    lv_modal_title_create(card, "Message");
    auto msg = lv_modal_message_create(card, data_->message().c_str());
    lv_obj_set_style_text_font(msg, messageFont(), 0);

    lv_modal_button_create(card, "Close", LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t*) {
        lv_async_call([card]() {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
            lv_modal_close(card);
        });
    });
}

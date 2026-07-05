/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "LanguageSelectScreen.hpp"
#include "widgets.hpp"
#include "NameCardKnot.hpp"
#include "Nvs.hpp"
#include "Strings.hpp"
#include "UiFont.hpp"
#include "bsp.h"

LanguageSelectScreen::LanguageSelectScreen(Mode mode, std::function<void()> on_continue)
    : mode_(mode), on_continue_(std::move(on_continue)) {}

void LanguageSelectScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
}

void LanguageSelectScreen::languageButtonCreate(lv_obj_t *parent, const char *label, const char *code) {
    bool active = mode_ == Mode::Settings && settings::language() == code;

    auto button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 90);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 10, 0);
    lv_obj_set_style_border_width(button, active ? 3 : 1, 0);

    auto text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, ui_font_32(), 0);

    if (active) {
        auto check = lv_label_create(button);
        lv_label_set_text(check, LV_SYMBOL_OK);
    }

    lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [this, code = std::string(code)](lv_event_t*) {
        onSelect(code);
    });
}

void LanguageSelectScreen::onSelect(const std::string &code) {
    if (mode_ == Mode::Boot) {
        settings::set_language(code);
        strings::set(code == "ja" ? strings::Lang::Ja : strings::Lang::En);
        if (on_continue_) on_continue_();
        return;
    }
    if (settings::language() == code) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
        screen_manager.pop();
        return;
    }
    settings::set_language(code);
    bsp_restart();
}

void LanguageSelectScreen::build() {
    auto card = lv_modal_open(root_);
    lv_modal_title_create(card, S().language_title);

    languageButtonCreate(card, "English", "en");
    languageButtonCreate(card, "日本語", "ja");
}

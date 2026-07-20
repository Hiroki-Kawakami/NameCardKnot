/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "SettingsScreen.hpp"
#include "AcknowledgementsScreen.hpp"
#include "DateTimeScreen.hpp"
#include "GrayscaleTestScreen.hpp"
#include "LanguageSelectScreen.hpp"
#include "NameCardKnot.hpp"
#include "Nvs.hpp"
#include "Strings.hpp"
#include "UiFont.hpp"
#include "resources.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

// ---- Editable about-page content ------------------------------------------
namespace {
constexpr const char kAuthorName[] = "Hiroki Kawakami";
constexpr const char kRepoUrl[] = "https://github.com/Hiroki-Kawakami/NameCardKnot";
constexpr const char kAuthorGitHub[] = "https://github.com/Hiroki-Kawakami";
constexpr const char kAuthorURL[] = "https://www.ideal-reality.com";
constexpr const char kAuthorSNS[] = "https://x.com/hiroki_cockatoo";
}  // namespace

static lv_obj_t *info_row_create(lv_obj_t *parent, const char *icon, const char *title, const char *value,
                                 std::function<void(lv_event_t*)> on_click) {
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 15, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_border_color(row, lv_color_white(), 0);
    lv_obj_set_style_border_color(row, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_add_event_fn(row, LV_EVENT_CLICKED, on_click);

    lv_obj_t *icon_label = lv_label_create(row);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, R.font.lucide_40, 0);

    lv_obj_t *col = lv_container_create(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_set_flex_grow(col, 1);

    lv_obj_t *title_label = lv_label_create(col);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, ui_font_24(), 0);

    if (value && value[0]) {
        lv_obj_t *value_label = lv_label_create(col);
        lv_label_set_text(value_label, value);
        lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
    }
    return row;
}

static lv_obj_t *settings_button_create(lv_obj_t *parent, const char *icon, const char *title,
                                        std::function<void(lv_event_t*)> on_click) {
    auto button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_style_border_color(button, lv_color_black(), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_width(button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(button, 15, 0);
    lv_obj_set_style_pad_all(button, 14, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 10, 0);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_add_event_fn(button, LV_EVENT_CLICKED, on_click);

    lv_obj_t *icon_label = lv_label_create(button);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, R.font.lucide_40, 0);

    lv_obj_t *title_label = lv_label_create(button);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, ui_font_24(), 0);

    return button;
}

void SettingsScreen::build() {
    createNavigation(S().settings);
    lv_obj_set_style_border_width(navigation_, 0, 0);
    lv_obj_set_style_pad_hor(contents_, 20, 0);
    lv_obj_set_style_pad_bottom(contents_, 20, 0);
    lv_obj_set_style_pad_row(contents_, 20, 0);

    // ---- App identity block ----
    {
        lv_obj_t *block = lv_container_create(contents_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(block, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(block, 1, 0);
        lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(block, 30, 0);

        lv_obj_t *mark = lv_label_create(block);
        lv_label_set_text_fmt(mark, "%s%s", LUCIDE_CONTACT_ROUND, LUCIDE_NFC);
        lv_obj_set_style_text_font(mark, R.font.lucide_40, 0);

        lv_obj_t *name = lv_label_create(block);
        lv_label_set_text(name, S().app_name);
        lv_obj_set_style_text_font(name, ui_font_48(), 0);
        lv_obj_set_style_margin_ver(name, 15, 0);

        lv_obj_t *version = lv_label_create(block);
        lv_label_set_text(version, "v0.1.0");
        lv_obj_set_style_text_font(version, ui_font_24(), 0);
        lv_obj_set_style_margin_bottom(version, 25, 0);

        lv_hor_separator_create(block, 10);
        info_row_create(block, LUCIDE_CODE, S().repository, kRepoUrl, [this](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
            auto card = lv_modal_open(root_);

            lv_obj_t *qr = lv_qrcode_create(card);
            lv_obj_set_width(qr, LV_PCT(100));
            lv_qrcode_set_size(qr, 300);
            lv_qrcode_set_dark_color(qr, lv_color_black());
            lv_qrcode_set_light_color(qr, lv_color_white());
            lv_qrcode_update(qr, kRepoUrl, sizeof(kRepoUrl) - 1);

            lv_obj_t *url = lv_label_create(card);
            lv_label_set_text(url, kRepoUrl);
            lv_obj_set_width(url, LV_PCT(100));
            lv_obj_set_style_pad_ver(url, 10, 0);
            lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);

            lv_modal_button_create(card, S().close, LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t*) {
                lv_async_call([card]() {
                    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
                    lv_modal_close(card);
                });
            });
        });
        lv_hor_separator_create(block, 10);
        info_row_create(block, LUCIDE_USER, kAuthorName, S().developer, [this](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
            auto card = lv_modal_open(root_);

            lv_obj_t *selector = lv_container_create(card, LV_FLEX_FLOW_ROW);
            lv_obj_set_width(selector, LV_PCT(100));
            lv_obj_set_flex_align(selector, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *qr = lv_qrcode_create(card);
            lv_obj_set_width(qr, LV_PCT(100));
            lv_qrcode_set_size(qr, 300);
            lv_qrcode_set_dark_color(qr, lv_color_black());
            lv_qrcode_set_light_color(qr, lv_color_white());

            lv_obj_t *url = lv_label_create(card);
            lv_obj_set_size(url, LV_PCT(100), 80);
            lv_obj_set_style_pad_ver(url, 10, 0);
            lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);

            auto show = [qr, url](const char *link) {
                lv_qrcode_update(qr, link, strlen(link));
                lv_label_set_text(url, link);
            };

            static const struct { const char *label; const char *link; } kLinks[] = {
                {"GitHub", kAuthorGitHub},
                {"Website", kAuthorURL},
                {"SNS", kAuthorSNS},
            };
            for (const auto &l : kLinks) {
                lv_obj_t *btn = lv_button_create(selector);
                lv_obj_remove_style_all(btn);
                lv_obj_set_flex_grow(btn, 1);
                lv_obj_set_style_pad_all(btn, 10, 0);
                lv_obj_set_style_border_color(btn, lv_color_white(), 0);
                lv_obj_set_style_border_color(btn, lv_color_black(), LV_STATE_PRESSED);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_t *text = lv_label_create(btn);
                lv_label_set_text(text, l.label);
                lv_obj_set_style_text_font(text, ui_font_24(), 0);
                lv_obj_center(text);
                const char *link = l.link;
                lv_obj_add_event_fn(btn, LV_EVENT_CLICKED, [show, link](lv_event_t*) {
                    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
                    show(link);
                });
            }
            show(kLinks[0].link);

            lv_modal_button_create(card, S().close, LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t*) {
                lv_async_call([card]() {
                    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
                    lv_modal_close(card);
                });
            });
        });
    }

    settings_button_create(contents_, LUCIDE_CLOCK, S().date_time, [](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
        screen_manager.push(std::make_shared<DateTimeScreen>(DateTimeScreen::Nav::Back));
    });
    settings_button_create(contents_, LUCIDE_LANGUAGES, S().languages, [](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
        screen_manager.push(std::make_shared<LanguageSelectScreen>(LanguageSelectScreen::Mode::Settings));
    });
    settings_button_create(contents_, LUCIDE_FLIP_VERTICAL, S().screen_orientation, [](lv_event_t*) {
        settings::set_display_flip(!settings::display_flip());
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_power_hw_reset();
        bsp_power_restart();
    });
    settings_button_create(contents_, LUCIDE_SCALE, S().acknowledgements, [](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
        screen_manager.push(std::make_shared<AcknowledgementsScreen>());
    });
}

void SettingsScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
}

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "DateTimeScreen.hpp"
#include "widgets.hpp"
#include "NameCardKnot.hpp"
#include "HomeScreen.hpp"
#include <cstdio>

static bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

// Sakamoto's algorithm; 0 = Sunday, matching bsp_rtc_datetime_t::weekday.
static int weekday_of(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year -= 1;
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

DateTimeScreen::DateTimeScreen(Nav nav) : nav_(nav) {
    bsp_rtc_datetime_t dt{};
    bool ok = bsp_rtc_get_time(&dt) == ESP_OK
        && dt.year >= 2020 && dt.year <= 2099
        && dt.month >= 1 && dt.month <= 12
        && dt.day >= 1 && dt.day <= days_in_month(dt.year, dt.month)
        && dt.hour <= 23 && dt.minute <= 59;
    if (ok) {
        year_ = dt.year; month_ = dt.month; day_ = dt.day; hour_ = dt.hour; minute_ = dt.minute;
    } else {
        year_ = 2026; month_ = 1; day_ = 1; hour_ = 0; minute_ = 0;
    }
}

void DateTimeScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
}

void DateTimeScreen::clampDay() {
    int max = days_in_month(year_, month_);
    if (day_ > max) day_ = max;
}

void DateTimeScreen::updateLabels() {
    char buf[8];
    snprintf(buf, sizeof buf, "%04d", year_);
    lv_label_set_text(year_label_, buf);
    snprintf(buf, sizeof buf, "%02d", month_);
    lv_label_set_text(month_label_, buf);
    snprintf(buf, sizeof buf, "%02d", day_);
    lv_label_set_text(day_label_, buf);
    snprintf(buf, sizeof buf, "%02d", hour_);
    lv_label_set_text(hour_label_, buf);
    snprintf(buf, sizeof buf, "%02d", minute_);
    lv_label_set_text(minute_label_, buf);
}

static constexpr int kColumnWidth = 136;

void DateTimeScreen::columnCreate(lv_obj_t *parent, const char *caption, lv_obj_t *&label,
                                   std::function<void(int)> step) {
    // Fixed pixel sizes throughout: a %-height column inside a content-height
    // row (the lv_container_create(parent, COLUMN) default) resolves to 0.
    auto col = lv_container_create(parent);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(col, kColumnWidth, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(col, 6, 0);

    auto plus = lv_button_create(col);
    lv_obj_set_size(plus, kColumnWidth, 56);
    lv_obj_add_event_fn(plus, LV_EVENT_CLICKED, [step](lv_event_t*) { step(1); });
    auto plus_label = lv_label_create(plus);
    lv_label_set_text(plus_label, "+");
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_32, 0);
    lv_obj_center(plus_label);

    label = lv_label_create(col);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);

    auto minus = lv_button_create(col);
    lv_obj_set_size(minus, kColumnWidth, 56);
    lv_obj_add_event_fn(minus, LV_EVENT_CLICKED, [step](lv_event_t*) { step(-1); });
    auto minus_label = lv_label_create(minus);
    lv_label_set_text(minus_label, "-");
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_32, 0);
    lv_obj_center(minus_label);

    auto cap = lv_label_create(col);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
}

void DateTimeScreen::commit() {
    bsp_rtc_datetime_t dt{};
    dt.year = (uint16_t)year_;
    dt.month = (uint8_t)month_;
    dt.day = (uint8_t)day_;
    dt.hour = (uint8_t)hour_;
    dt.minute = (uint8_t)minute_;
    dt.second = 0;
    dt.weekday = (uint8_t)weekday_of(year_, month_, day_);
    bsp_rtc_set_time(&dt);
    rtc_sync_system_time();

    if (nav_ == Nav::Back) {
        screen_manager.pop();
    } else {
        screen_manager.load(std::make_shared<HomeScreen>());
    }
}

void DateTimeScreen::build() {
    auto card = lv_modal_open(root_);
    lv_modal_title_create(card, "Date & Time");

    auto row1 = lv_container_create(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 8, 0);
    columnCreate(row1, "Year", year_label_, [this](int d) {
        year_ += d;
        if (year_ > 2099) year_ = 2020;
        if (year_ < 2020) year_ = 2099;
        clampDay();
        updateLabels();
    });
    columnCreate(row1, "Month", month_label_, [this](int d) {
        month_ += d;
        if (month_ > 12) month_ = 1;
        if (month_ < 1) month_ = 12;
        clampDay();
        updateLabels();
    });
    columnCreate(row1, "Day", day_label_, [this](int d) {
        int max = days_in_month(year_, month_);
        day_ += d;
        if (day_ > max) day_ = 1;
        if (day_ < 1) day_ = max;
        updateLabels();
    });

    auto row2 = lv_container_create(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row2, 8, 0);
    columnCreate(row2, "Hour", hour_label_, [this](int d) {
        hour_ += d;
        if (hour_ > 23) hour_ = 0;
        if (hour_ < 0) hour_ = 23;
        updateLabels();
    });
    columnCreate(row2, "Minute", minute_label_, [this](int d) {
        minute_ += d;
        if (minute_ > 59) minute_ = 0;
        if (minute_ < 0) minute_ = 59;
        updateLabels();
    });

    updateLabels();

    auto buttons = lv_container_create(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 10, 0);
    if (nav_ == Nav::Back) {
        lv_modal_button_create(buttons, "Cancel", LV_MODAL_BUTTON_TYPE_PRIMARY,
                                [](lv_event_t*) { screen_manager.pop(); });
    }
    lv_modal_button_create(buttons, "OK", LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t*) { commit(); });
}

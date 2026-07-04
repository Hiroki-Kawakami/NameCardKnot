/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include <functional>

class DateTimeScreen : public Screen {
public:
    enum class Nav { Boot, Back };
    explicit DateTimeScreen(Nav nav);
    void build() override;
    void onAppear() override;

private:
    Nav nav_;
    int year_, month_, day_, hour_, minute_;
    lv_obj_t *year_label_ = nullptr;
    lv_obj_t *month_label_ = nullptr;
    lv_obj_t *day_label_ = nullptr;
    lv_obj_t *hour_label_ = nullptr;
    lv_obj_t *minute_label_ = nullptr;

    void clampDay();
    void updateLabels();
    void commit();
    void columnCreate(lv_obj_t *parent, const char *caption, lv_obj_t *&label, std::function<void(int)> step);
};

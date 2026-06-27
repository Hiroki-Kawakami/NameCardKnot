/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"

class ShareScreen : public NavigationScreen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    lv_obj_t *value_label_;
    lv_obj_t *status_label_;
    int value_ = 0;
    bool sent_ = false;
};

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"

// Third-party license viewer: one wrapped label clipped to a viewport, paged a
// viewport-height at a time by buttons (no touch-drag — EPD can't animate).
class AcknowledgementsScreen : public NavigationScreen {
public:
    void build() override;

private:
    lv_obj_t *viewport_ = nullptr;
    lv_obj_t *page_label_ = nullptr;
    lv_obj_t *prev_button_ = nullptr;
    lv_obj_t *next_button_ = nullptr;
    int page_ = 0;
    int page_count_ = 1;
    int page_height_ = 0;
    int scroll_max_ = 0;

    void showPage(int page);
};

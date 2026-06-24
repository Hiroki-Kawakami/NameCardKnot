/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "widgets.hpp"

class FileBrowserScreen : public NavigationScreen {
public:
    void build() override;
    void rebuild();
};

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include <string>

class NameCardScreen : public Screen {
public:
    explicit NameCardScreen(std::string path);
    void build() override;
    void onAppear() override;

private:
    std::string path_;

    void openMenu();
};

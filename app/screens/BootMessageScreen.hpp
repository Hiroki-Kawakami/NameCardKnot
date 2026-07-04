/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "Nvs.hpp"
#include <functional>
#include <string>

// Generic modal message shown at boot, branched on bootmsg::Id. Boot mode adds
// an OK button that clears the display and continues; ResetFailed has no
// button (touch is dead) and hints at the physical reset button instead.
class BootMessageScreen : public Screen {
public:
    enum class Mode { Boot, ResetFailed };

    BootMessageScreen(bootmsg::Id id, std::string param, Mode mode,
                       std::function<void()> on_continue = nullptr);
    void build() override;
    void onAppear() override;

    void refreshModal();

private:
    bootmsg::Id id_;
    std::string param_;
    Mode mode_;
    std::function<void()> on_continue_;
    lv_obj_t *modal_ = nullptr;
};

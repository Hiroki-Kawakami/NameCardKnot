/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include <functional>
#include <string>

// Language-neutral picker (title always shown bilingually). Boot mode is the
// first-boot gate: tapping a language sets it and runs the continuation, no
// restart. Settings mode is pushed from SettingsScreen: tapping the already
// active language just pops back; tapping the other one restarts the device
// so every already-built screen picks up the new Strings table.
class LanguageSelectScreen : public Screen {
public:
    enum class Mode { Boot, Settings };
    explicit LanguageSelectScreen(Mode mode, std::function<void()> on_continue = nullptr);
    void build() override;
    void onAppear() override;

private:
    Mode mode_;
    std::function<void()> on_continue_;

    void languageButtonCreate(lv_obj_t *parent, const char *label, const char *code);
    void onSelect(const std::string &code);
};

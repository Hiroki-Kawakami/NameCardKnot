/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "NameCardData.hpp"
#include "NameFont.hpp"
#include <memory>

class NameCardScreen : public Screen {
public:
    // Back: pushed from the browser, bottom-left menu button pops back to it.
    // Home: loaded from Home / boot resume, the button loads HomeScreen.
    enum class Nav { Back, Home };
    // Initial menu state: Open teaches the menu's existence (browser open, boot
    // resume); Closed for the familiar Home -> My Card path.
    enum class Menu { Closed, Open };

    NameCardScreen(std::shared_ptr<NameCardData> data, Nav nav, Menu menu);
    ~NameCardScreen() override;
    void build() override;
    void onEnter() override;
    void onExit() override;
    void onAppear() override;

    void clearDisplay();
    bool closeOverlays();  // hide menu + close info modal; true if anything was open
    // The screen is showing just the card image (no menu/modal): a repaint in
    // this state leaves the glass seedable for the next boot.
    bool bareCardShown() const;
    // Bottom-sheet menu, toggled by tapping the card. Its own rect is the only
    // dirty area, so open/close refreshes stay off the card pixels.
    void openMenu();
    void closeMenu(bool full_refresh);
    // Boot resume onto a seeded panel: the first paint refreshes nothing; the
    // caller then reveals the menu (openMenu) so only its rect is driven.
    void set_resume_seeded() { seeded_ = true; }

    const std::shared_ptr<NameCardData> &data() const { return data_; }

private:
    std::shared_ptr<NameCardData> data_;  // owns the decoded image + metadata
    Nav nav_;
    Menu initial_menu_;
    bool seeded_ = false;
    lv_image_dsc_t dsc_{};   // references the display image's buffer (kept alive by data_)
    lv_timer_t *poll_ = nullptr;  // boot resume: data_ may still be decoding
    lv_obj_t *menu_ = nullptr;    // bottom menu (hidden when closed, never deleted)
    lv_obj_t *modal_ = nullptr;   // open info modal

    // The name label's font chain, built lazily and cached (its mutable font
    // copies must outlive any label using them).
    std::unique_ptr<NameFont> name_font_;
    const lv_font_t *nameFont();

    bool menuIsOpen() const;
    void buildMenu();
    void showImage();
    void poll();
    void leave();
    void openInfo();
};

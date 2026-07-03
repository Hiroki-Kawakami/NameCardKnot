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

    NameCardScreen(std::shared_ptr<NameCardData> data, Nav nav);
    ~NameCardScreen() override;
    void build() override;
    void onEnter() override;
    void onExit() override;
    void onAppear() override;
    void onDisappear() override;

    void clearDisplay();
    bool closeOverlays();  // hide menu + close info modal; true if anything was open
    // Bottom-sheet menu, toggled by tapping the card. Its own rect is the only
    // dirty area, so open/close refreshes stay off the card pixels.
    void openMenu();
    void closeMenu(bool full_refresh);
    void refreshMenu();
    // Boot resume onto the card the glass still shows: onAppear's first paint
    // refreshes nothing, then the caller drives only the menu rect (refreshMenu),
    // so the card is never re-flashed.
    void set_clean_resume(bool clean_resume) { clean_resume_ = clean_resume; }

    const std::shared_ptr<NameCardData> &data() const { return data_; }

private:
    std::shared_ptr<NameCardData> data_;  // owns the decoded image + metadata
    Nav nav_;
    bool clean_resume_ = false;
    lv_image_dsc_t dsc_{};   // references the display image's buffer (kept alive by data_)
    lv_timer_t *poll_ = nullptr;  // boot resume: data_ may still be decoding
    lv_obj_t *menu_ = nullptr;    // bottom menu (hidden when closed, never deleted)
    lv_obj_t *modal_ = nullptr;   // open info modal
    lv_obj_t *contents_ = nullptr;   // the card image (or a fallback label)

    // The name label's font chain, built lazily and cached (its mutable font
    // copies must outlive any label using them).
    std::unique_ptr<NameFont> name_font_;
    const lv_font_t *nameFont();

    bool cleanResumable() const;
    bool menuIsOpen() const;
    void buildMenu();
    void showImage();
    bool imageLoaded() const { return contents_ != nullptr; }
    void poll();
    void leave();
    void openInfo();
};

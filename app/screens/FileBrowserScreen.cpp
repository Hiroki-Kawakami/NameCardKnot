/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "FileBrowserScreen.hpp"
#include "NameCardScreen.hpp"
#include "NameCardData.hpp"
#include "ImportJob.hpp"
#include "NameCardKnot.hpp"
#include "resources.h"
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

FileBrowserScreen::FileBrowserScreen(std::string path, Mode mode)
    : mode_(mode), path_stack_({path}) {}

void FileBrowserScreen::build() {
    createNavigation("");
    load();
    rebuild();
}

void FileBrowserScreen::back() {
    if (path_stack_.size() > 1) {
        path_stack_.pop_back();
        load();
        rebuild();
    } else {
        screen_manager.pop();
    }
}

void FileBrowserScreen::load() {
    entries_.clear();
    error_.clear();
    offset_ = 0;

    if (!mount_sd_card()) {
        error_ = "SD card not found.";
        return;
    }

    auto path = path_stack_.back();
    DIR *dir = opendir(path.c_str());
    if (!dir) {
        error_ = "Cannot open " + path;
        // The card may have been pulled: unmount so the next Refresh retries
        // the mount instead of reusing a dead filesystem.
        if (path_stack_.empty() == 1) unmount_sd_card();
        return;
    }
    while (auto *ent = readdir(dir)) {
        if (ent->d_name[0] == '.') continue;
        bool is_dir;
        if (ent->d_type == DT_DIR) {
            is_dir = true;
        } else if (ent->d_type == DT_REG) {
            is_dir = false;
        } else {
            struct stat st;
            std::string full = path + "/" + ent->d_name;
            if (stat(full.c_str(), &st) != 0) continue;
            is_dir = S_ISDIR(st.st_mode);
        }
        if (!accept(ent->d_name, is_dir)) continue;
        entries_.push_back(Entry{ent->d_name, is_dir});
    }
    closedir(dir);
    std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
        if (a.dir != b.dir) return a.dir;  // folders first
        const std::string &x = a.name, &y = b.name;  // case-insensitive name compare
        for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
            int cx = std::tolower((unsigned char)x[i]);
            int cy = std::tolower((unsigned char)y[i]);
            if (cx != cy) return cx < cy;
        }
        return x.size() < y.size();
    });
}

void FileBrowserScreen::rebuild() {
    lv_obj_clean(contents_);
    const std::string &path = path_stack_.back();
    size_t slash = path.find_last_of('/');
    lv_label_set_text(navigation_title_, slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1);

    if (entries_.empty()) {
        auto label = lv_label_create(contents_);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, "No Items");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_margin_top(label, 40, 0);
        return;
    }

    size_t page_item_num = 10;
    int page_num = (entries_.size() + page_item_num - 1) / 10;
    int page = offset_ / page_item_num;
    bool has_prev_page = page > 0;
    bool has_next_page = page + 1 < page_num;

    for (int i = 0; i < page_item_num; i++) {
        if (i > 0) lv_hor_separator_create(contents_);
        if (offset_ + i >= entries_.size()) {
            lv_spacer_create(contents_, LV_PCT(100), 0, 1);
            break;
        }

        auto &e = entries_[offset_ + i];
        auto row = lv_button_create(contents_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 77);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_color(row, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_set_user_data(row, (void*)(offset_ + i));
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            auto self = (FileBrowserScreen*)lv_event_get_user_data(e);
            auto button = lv_event_get_target_obj(e);
            auto index = (size_t)lv_obj_get_user_data(button);
            self->open(index);
        }, LV_EVENT_CLICKED, this);

        auto icon_label = lv_label_create(row);
        lv_obj_set_width(icon_label, 48);
        lv_label_set_text(icon_label, e.dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);

        auto label = lv_label_create(row);
        lv_label_set_text(label, e.name.c_str());
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    }

    auto status = lv_container_create(contents_);
    lv_obj_set_size(status, LV_PCT(100), 81);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(status, 2, 0);
    lv_obj_set_style_border_color(status, lv_color_black(), 0);

    auto label = lv_label_create(status);
    auto status_str = std::to_string(entries_.size()) + " Items ";
    status_str += "(" + std::to_string(page + 1) + "/" + std::to_string(page_num) + ")";
    lv_label_set_text(label, status_str.c_str());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);

    if (has_prev_page) {
        auto button = lv_button_create(status);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 60, 60);
        lv_obj_align(button, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_border_color(button, lv_color_white(), 0);
        lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [this](lv_event_t*) {
            offset_ -= 10;
            rebuild();
        });

        auto icon = lv_label_create(button);
        lv_obj_center(icon);
        lv_label_set_text(icon, LUCIDE_CIRCLE_ARROW_LEFT);
        lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);
    }
    if (has_next_page) {
        auto button = lv_button_create(status);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 60, 60);
        lv_obj_align(button, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_border_color(button, lv_color_white(), 0);
        lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, [this](lv_event_t*) {
            offset_ += 10;
            rebuild();
        });

        auto icon = lv_label_create(button);
        lv_obj_center(icon);
        lv_label_set_text(icon, LUCIDE_CIRCLE_ARROW_RIGHT);
        lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);
    }
}

static bool is_image(const std::string &name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp";
}

static bool ends_with(const std::string &s, const char *suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool FileBrowserScreen::accept(const std::string &name, bool is_dir) const {
    if (is_dir) return true;
    // Import lists only the .mnc.pdf containers it can save as a My Card.
    if (mode_ == Mode::ImportMyCard) return ends_with(name, ".mnc.pdf");
    return true;
}

void FileBrowserScreen::open(int index) {
    auto &e = entries_[index];
    auto path = path_stack_.back() + "/" + e.name;
    if (e.dir) {
        path_stack_.emplace_back(path);
        load();
        rebuild();
        return;
    }

    lv_display_t *disp = lv_display_get_default();
    uint16_t dw = lv_display_get_horizontal_resolution(disp);
    uint16_t dh = lv_display_get_vertical_resolution(disp);

    // Per mode/file-type: pick the loader (data-class init) + the completion
    // transition. The modal + poll loop around them is common (openProgress).
    if (mode_ == Mode::ImportMyCard) {
        if (!ends_with(e.name, ".mnc.pdf")) return;
        loader_ = ImportJob::start(path, dw, dh);
        onLoaded_ = [] {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            screen_manager.pop();  // back to Home; onAppear reflects the new card
        };
        openProgress();
        return;
    }

    // Open mode: view the image/card 1:1 (decode at the display resolution).
    if (is_image(e.name) || ends_with(e.name, ".mnc.pdf")) {
        imgproc::Options opts;
        opts.target_w = dw;
        opts.target_h = dh;
        opts.fit = imgproc::Fit::Contain;
        opts.levels = 16;
        auto data = NameCardData::load(path, opts);
        loader_ = data;
        onLoaded_ = [this, data] {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            screen_manager.push(std::make_shared<NameCardScreen>(data));
        };
        openProgress();
    }
    // .snc.pdf (no display image) and other types are not openable yet.
}

void FileBrowserScreen::openProgress() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
    card_ = lv_modal_open(root_);
    lv_modal_title_create(card_, "Loading...");
    lv_modal_message_create(card_, loader_->label().c_str());

    bar_ = lv_bar_create(card_);
    lv_obj_set_width(bar_, LV_PCT(100));
    lv_bar_set_min_value(bar_, 0);
    lv_bar_set_max_value(bar_, 100);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);

    lv_modal_button_create(card_, "Cancel", LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t *e) {
        if (loader_) loader_->cancel();
        cancelling_ = true;  // keep the modal up until the worker acknowledges
        auto button = lv_event_get_target_obj(e);
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(button, 0), "Cancelling...");
    });

    cancelling_ = false;
    last_pct_ = 0;
    last_tick_ = lv_tick_get();
    poll_ = lv_timer_create([](lv_timer_t *t) {
        static_cast<FileBrowserScreen *>(lv_timer_get_user_data(t))->poll();
    }, 100, this);
}

void FileBrowserScreen::poll() {
    if (!loader_) return;
    auto state = loader_->state();

    if (state == FileLoader::State::Loading) {
        if (cancelling_) return;  // waiting for the worker to stop
        // The decode runs on its own core, so a bar refresh only costs PSRAM
        // contention (not CPU preemption); ~1s between refreshes is cheap enough.
        static constexpr uint32_t kBarRefreshMs = 1000;
        int pct = loader_->progress_pct();
        if (pct != last_pct_ && lv_tick_elaps(last_tick_) >= kBarRefreshMs) {
            lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
            last_pct_ = pct;
            last_tick_ = lv_tick_get();
        }
        return;
    }

    // Terminal: on Ok run the completion callback (it holds the loaded data);
    // cancelled / failed just dismiss the modal.
    bool ok = state == FileLoader::State::Ok && !cancelling_;
    auto cb = onLoaded_;  // keep a copy; stopLoad clears the member
    stopLoad();
    lv_modal_close(card_);
    card_ = nullptr;
    bar_ = nullptr;
    if (ok && cb) cb();
}

void FileBrowserScreen::stopLoad() {
    if (poll_) { lv_timer_delete(poll_); poll_ = nullptr; }
    loader_.reset();
    onLoaded_ = nullptr;
    cancelling_ = false;
}

FileBrowserScreen::~FileBrowserScreen() {
    if (loader_) loader_->cancel();  // worker holds its own ref; it finishes on its own
    stopLoad();
}

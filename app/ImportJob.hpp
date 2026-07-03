/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "FileLoader.hpp"
#include "image_processor.hpp"
#include <atomic>
#include <memory>
#include <string>

// Imports a .mnc.pdf into the My Card flash partition: stores the full PDF and
// the decoded display/preview/name caches (see CardStore). A FileLoader, so it
// drops straight into FileBrowserScreen's progress modal + poll loop. Runs on
// its own worker task (the worker holds a shared_ptr ref, so the job outlives a
// dropped handle, like imgproc::DecodeJob). The only LVGL-touching step (the
// offscreen name raster) takes the LVGL lock.
class ImportJob : public FileLoader {
public:
    // disp_w/h: the on-screen display-image size (matches an SD open's decode).
    static std::shared_ptr<ImportJob> start(const std::string &sd_path,
                                            uint16_t disp_w, uint16_t disp_h);
    ~ImportJob() override = default;

    State state() const override { return state_.load(); }
    int progress_pct() const override;
    void cancel() override;
    std::string label() const override { return name_; }

    void run();  // internal: the worker body

private:
    ImportJob() = default;
    void set_phase(int base, int span);  // span 0 -> a fixed milestone at `base`

    std::string path_;
    std::string name_;  // modal label (the file name; immutable after start)
    uint16_t disp_w_ = 0, disp_h_ = 0;

    imgproc::Progress prog_;  // live progress of the active decode phase
    std::atomic<int>   phase_base_{0};
    std::atomic<int>   phase_span_{0};
    std::atomic<State> state_{State::Loading};
};

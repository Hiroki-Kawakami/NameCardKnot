/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "FileLoader.hpp"
#include "L8View.hpp"
#include "MyCardStore.hpp"
#include "SharedCardData.hpp"
#include "image_processor.hpp"
#include "namecard_pdf.hpp"
#include <memory>
#include <string>
#include <vector>

// Async loader (the browser's FileLoader) over a plain image or a .mnc.pdf:
// decodes the display image off the UI task and, for a card, carries the
// metadata. LVGL-free; NameCardScreen wraps display_image() into an lv_image.
class NameCardData : public FileLoader {
public:
    enum class Kind { Image, Card };

    static std::shared_ptr<NameCardData> load(const std::string &path,
                                              const imgproc::Options &opts);

    // Build from the saved My Card (mycard flash partition) with no decode: the
    // display image is viewed straight from MMIO and metadata is parsed from the
    // mapped PDF. Returns nullptr when no card is stored. State is Ok at once.
    static std::shared_ptr<NameCardData> load_cached();

    ~NameCardData();
    NameCardData(const NameCardData &) = delete;
    NameCardData &operator=(const NameCardData &) = delete;

    State state() const override;
    int progress_pct() const override;
    void cancel() override;
    std::string label() const override;
    imgproc::Status status() const;  // failure detail when state()==Failed

    Kind kind() const { return kind_; }
    const std::string &path() const { return path_; }

    // Owned here — keep the shared_ptr alive while an lv_image references it.
    const imgproc::Image &display_image() const;

    // The display image as a non-owning L8 view, unified across both sources:
    // for an SD load it points into the decoded image_, for a cached card into
    // the mmap'd flash blob. The shared_ptr (and, for a cached card, the store's
    // mapping) must outlive any lv_image built from it.
    const L8View &display_view() const;

    bool is_card() const { return kind_ == Kind::Card; }
    const std::string &name() const { return card_.name; }
    const std::string &url() const { return card_.url; }

    std::shared_ptr<SharedCardData> share() const;  // nullptr if this is not a card

    // The embedded rare-kanji glyph supplement, or nullptr when the card has
    // none (the common case). References glyph_blob_, so it lives as long as this.
    const nckpdf::GlyphSet *name_glyphs() const { return has_glyphs_ ? &glyphs_ : nullptr; }

private:
    NameCardData() = default;
    void finalize() const;  // pull the image out of the worker once it is terminal
    void load_name_glyphs();

    Kind kind_ = Kind::Image;
    std::string path_;
    nckpdf::Card card_;
    std::vector<uint8_t> glyph_blob_;  // raw name_glyphs bytes (glyphs_ points in)
    nckpdf::GlyphSet glyphs_;
    bool has_glyphs_ = false;
    std::vector<uint8_t> pdf_;            // cached card: the PDF copy (glyphs_ point in)
    mycard::MappedImage display_map_;     // cached card: MMIO display image (view_ points in)
    std::shared_ptr<imgproc::DecodeJob> job_;
    mutable imgproc::Image image_;
    mutable L8View view_;  // display image as L8 (image_ for SD, mmap for cached)
    mutable State state_ = State::Loading;
    mutable imgproc::Status status_ = imgproc::Status::Ok;
    mutable bool finalized_ = false;
};

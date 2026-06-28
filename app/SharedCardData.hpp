/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "image_processor.hpp"
#include "namecard_pdf.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Forward-only reader over the share-only PDF (the container truncated at
// base_total_length, 0 = whole file), for streaming to a peer without holding the
// bytes in RAM. read() returns 0 once drained or on an I/O failure (latches error()).
class SharePdfStream {
public:
    ~SharePdfStream();
    SharePdfStream(const SharePdfStream &) = delete;
    SharePdfStream &operator=(const SharePdfStream &) = delete;

    bool valid() const { return file_ != nullptr; }
    size_t size() const { return end_; }
    size_t remaining() const { return end_ - pos_; }
    bool error() const { return error_; }
    size_t read(void *buf, size_t cap);

private:
    friend class SharedCardData;
    SharePdfStream(const std::string &path, size_t limit);

    std::FILE *file_ = nullptr;
    size_t end_ = 0;
    size_t pos_ = 0;
    bool error_ = false;
};

// The share-only half of a NameCardKnot container PDF: metadata + share JPEG(s),
// no display image. Parses metadata up front and decodes a share image on demand.
// File-backed only for now. See docs/namecard_pdf.md.
class SharedCardData {
public:
    static std::shared_ptr<SharedCardData> open(const std::string &path);

    ~SharedCardData();
    SharedCardData(const SharedCardData &) = delete;
    SharedCardData &operator=(const SharedCardData &) = delete;

    bool valid() const { return status_ == nckpdf::Status::Ok; }
    nckpdf::Status status() const { return status_; }
    const std::string &path() const { return path_; }

    const std::string &name() const { return card_.name; }
    const std::string &url() const { return card_.url; }
    const std::string &message() const { return card_.message; }
    const nckpdf::GlyphSet *name_glyphs() const { return has_glyphs_ ? &glyphs_ : nullptr; }

    int share_image_count() const { return static_cast<int>(share_assets_.size()); }

    // Synchronous: the editor pre-sizes these JPEGs, so an async/progress path
    // would cost more EPD refreshes than it saves.
    imgproc::Status decode_share_image(int index, const imgproc::Options &opts,
                                       imgproc::Image &out) const;

    std::unique_ptr<SharePdfStream> share_stream() const;

private:
    SharedCardData() = default;
    void load_name_glyphs();

    std::string path_;
    nckpdf::Card card_;
    std::vector<nckpdf::Asset> share_assets_;
    std::vector<uint8_t> glyph_blob_;
    nckpdf::GlyphSet glyphs_;
    bool has_glyphs_ = false;
    nckpdf::Status status_ = nckpdf::Status::Ok;
};

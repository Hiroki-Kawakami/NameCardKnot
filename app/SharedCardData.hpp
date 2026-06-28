/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "MyCardStore.hpp"
#include "image_processor.hpp"
#include "namecard_pdf.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Forward-only reader over the share-only PDF (the container truncated at
// base_total_length, 0 = whole source), for streaming to a peer without holding
// the bytes in RAM. read() returns 0 once drained or on an I/O failure (error()).
class SharePdfStream {
public:
    ~SharePdfStream();
    SharePdfStream(const SharePdfStream &) = delete;
    SharePdfStream &operator=(const SharePdfStream &) = delete;

    bool valid() const { return ok_; }
    size_t size() const { return end_; }
    size_t remaining() const { return end_ - pos_; }
    bool error() const { return error_; }
    size_t read(void *buf, size_t cap);

private:
    friend class SharedCardData;
    SharePdfStream(const std::string &path, size_t limit);
    SharePdfStream(mycard::BlobId blob, size_t limit);

    std::FILE *file_ = nullptr;
    mycard::BlobId blob_ = mycard::BLOB_PDF;
    bool mycard_ = false;
    bool ok_ = false;
    bool error_ = false;
    size_t end_ = 0;
    size_t pos_ = 0;
};

// The share-only half of a NameCardKnot container PDF: metadata + share JPEG(s),
// no display image. Parses metadata up front and decodes a share image on demand.
// Backed by an SD file (open) or the cached My Card flash blob (from_mycard).
// See docs/namecard_pdf.md.
class SharedCardData {
public:
    static std::shared_ptr<SharedCardData> open(const std::string &path);
    static std::shared_ptr<SharedCardData> from_mycard(const nckpdf::Card &card);

    ~SharedCardData();
    SharedCardData(const SharedCardData &) = delete;
    SharedCardData &operator=(const SharedCardData &) = delete;

    bool valid() const { return status_ == nckpdf::Status::Ok; }
    nckpdf::Status status() const { return status_; }

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
    void init_shared();
    void load_name_glyphs();
    size_t read_source(uint32_t offset, void *buf, size_t len) const;

    bool mycard_ = false;          // false: file-backed (path_); true: BLOB_PDF
    std::string path_;
    nckpdf::Card card_;
    std::vector<nckpdf::Asset> share_assets_;
    std::vector<uint8_t> glyph_blob_;
    nckpdf::GlyphSet glyphs_;
    bool has_glyphs_ = false;
    nckpdf::Status status_ = nckpdf::Status::Ok;
};

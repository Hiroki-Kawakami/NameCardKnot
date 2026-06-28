/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "SharedCardData.hpp"

SharePdfStream::SharePdfStream(const std::string &path, size_t limit) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) return;
    if (limit) {
        end_ = limit;
    } else if (std::fseek(file_, 0, SEEK_END) == 0) {
        long sz = std::ftell(file_);
        end_ = sz > 0 ? static_cast<size_t>(sz) : 0;
        std::rewind(file_);
    }
}

SharePdfStream::~SharePdfStream() {
    if (file_) std::fclose(file_);
}

size_t SharePdfStream::read(void *buf, size_t cap) {
    if (!file_ || error_) return 0;
    size_t want = remaining();
    if (want > cap) want = cap;
    if (!want) return 0;
    size_t got = std::fread(buf, 1, want, file_);
    pos_ += got;
    if (got != want) error_ = true;
    return got;
}

std::shared_ptr<SharedCardData> SharedCardData::open(const std::string &path) {
    auto self = std::shared_ptr<SharedCardData>(new SharedCardData());
    self->path_ = path;
    self->status_ = nckpdf::open_file(path.c_str(), self->card_);
    if (self->status_ != nckpdf::Status::Ok) return self;
    for (const nckpdf::Asset *a : self->card_.all(nckpdf::AssetType::ShareJpeg))
        self->share_assets_.push_back(*a);
    self->load_name_glyphs();
    return self;
}

SharedCardData::~SharedCardData() = default;

void SharedCardData::load_name_glyphs() {
    const nckpdf::Asset *a = card_.find(nckpdf::AssetType::NameGlyphs);
    if (!a || a->length == 0) return;
    glyph_blob_.resize(a->length);
    if (nckpdf::read_asset(path_.c_str(), *a, glyph_blob_.data(), glyph_blob_.size()) != nckpdf::Status::Ok)
        return;
    if (nckpdf::parse_name_glyphs(glyph_blob_.data(), glyph_blob_.size(), glyphs_) != nckpdf::Status::Ok)
        return;
    has_glyphs_ = true;
}

imgproc::Status SharedCardData::decode_share_image(int index, const imgproc::Options &opts,
                                                   imgproc::Image &out) const {
    if (index < 0 || index >= share_image_count()) return imgproc::Status::BadArgument;
    const nckpdf::Asset &a = share_assets_[index];
    return imgproc::decode_file(path_.c_str(), opts, out, nullptr, a.offset, a.length);
}

std::unique_ptr<SharePdfStream> SharedCardData::share_stream() const {
    auto s = std::unique_ptr<SharePdfStream>(new SharePdfStream(path_, card_.base_total_length));
    if (!s->valid()) return nullptr;
    return s;
}

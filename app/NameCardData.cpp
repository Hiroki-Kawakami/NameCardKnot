/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameCardData.hpp"

#define NCD_TASK_PRIORITY 3
#define NCD_TASK_CORE 1

std::shared_ptr<NameCardData> NameCardData::load(const std::string &path,
                                                 const imgproc::Options &opts) {
    auto self = std::shared_ptr<NameCardData>(new NameCardData());
    self->path_ = path;

    // A .mnc.pdf is detected by its NCK footer (small read, no image bytes); a
    // plain image reports NotNckPdf and is decoded whole.
    nckpdf::Card card;
    nckpdf::Status pst = nckpdf::open_file(path.c_str(), card);
    if (pst == nckpdf::Status::Ok) {
        const nckpdf::Asset *disp = card.find(nckpdf::AssetType::DisplayJpeg);
        if (!disp) {  // no display image (e.g. .snc.pdf)
            self->state_ = State::Failed;
            self->status_ = imgproc::Status::UnsupportedFormat;
            return self;
        }
        const uint32_t off = disp->offset;
        const uint32_t len = disp->length;
        self->kind_ = Kind::Card;
        self->card_ = std::move(card);
        self->load_name_glyphs();
        self->job_ = imgproc::decode_file_async(path.c_str(), opts, NCD_TASK_PRIORITY, NCD_TASK_CORE,
                                                off, len);
    } else if (pst == nckpdf::Status::NotNckPdf) {
        self->kind_ = Kind::Image;
        self->job_ =
            imgproc::decode_file_async(path.c_str(), opts, NCD_TASK_PRIORITY, NCD_TASK_CORE);
    } else {  // a corrupt / unsupported NCK container
        self->state_ = State::Failed;
        self->status_ = imgproc::Status::DecodeError;
    }
    return self;
}

void NameCardData::load_name_glyphs() {
    const nckpdf::Asset *a = card_.find(nckpdf::AssetType::NameGlyphs);
    if (!a || a->length == 0) return;  // most names need no supplement
    glyph_blob_.resize(a->length);
    if (nckpdf::read_asset(path_.c_str(), *a, glyph_blob_.data(), glyph_blob_.size()) != nckpdf::Status::Ok)
        return;
    if (nckpdf::parse_name_glyphs(glyph_blob_.data(), glyph_blob_.size(), glyphs_) != nckpdf::Status::Ok)
        return;
    has_glyphs_ = true;
}

NameCardData::~NameCardData() {
    if (job_) job_->cancel();  // the worker holds its own ref; it finishes on its own
}

void NameCardData::finalize() const {
    if (finalized_ || !job_) return;
    switch (job_->state()) {
        case imgproc::DecodeJob::State::Running:
            return;
        case imgproc::DecodeJob::State::Ok:
            image_ = job_->take_image();
            state_ = State::Ok;
            break;
        case imgproc::DecodeJob::State::Cancelled:
            state_ = State::Cancelled;
            break;
        default:
            state_ = State::Failed;
            break;
    }
    status_ = job_->status();
    finalized_ = true;
}

NameCardData::State NameCardData::state() const {
    if (state_ == State::Loading) finalize();
    return state_;
}

int NameCardData::progress_pct() const {
    return job_ ? job_->progress_pct() : 0;
}

void NameCardData::cancel() {
    if (job_) job_->cancel();
}

imgproc::Status NameCardData::status() const {
    finalize();
    return status_;
}

std::string NameCardData::label() const {
    if (kind_ == Kind::Card && !card_.name.empty()) return card_.name;
    const auto slash = path_.find_last_of('/');
    return slash == std::string::npos ? path_ : path_.substr(slash + 1);
}

const imgproc::Image &NameCardData::display_image() const {
    finalize();
    return image_;
}

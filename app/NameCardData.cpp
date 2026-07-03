/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameCardData.hpp"
#include "CardStore.hpp"

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

// Parse pdf_ into card_ + the in-place glyph supplement (pdf_ outlives this).
bool NameCardData::adopt_pdf() {
    if (pdf_.empty() ||
        nckpdf::parse_buffer(pdf_.data(), pdf_.size(), card_) != nckpdf::Status::Ok)
        return false;
    const nckpdf::Asset *a = card_.find(nckpdf::AssetType::NameGlyphs);
    if (a && a->length && uint64_t(a->offset) + a->length <= pdf_.size() &&
        nckpdf::parse_name_glyphs(pdf_.data() + a->offset, a->length, glyphs_) == nckpdf::Status::Ok)
        has_glyphs_ = true;
    return true;
}

std::shared_ptr<NameCardData> NameCardData::load_cached() {
    auto &st = cardstore::mycard();
    if (!st.available()) return nullptr;
    auto self = std::shared_ptr<NameCardData>(new NameCardData());
    self->kind_ = Kind::Card;

    // Metadata: copy the stored PDF into RAM and parse it (no decode).
    uint32_t pdf_len = st.blob_len(cardstore::BLOB_PDF);
    self->pdf_.resize(pdf_len);
    if (!pdf_len || !st.read_blob(cardstore::BLOB_PDF, self->pdf_.data(), pdf_len) ||
        !self->adopt_pdf())
        self->pdf_.clear();  // metadata unavailable; the mapped image may still show

    // Display image mapped on demand (held by display_map_ for this object's life).
    self->display_map_ = st.map_image(cardstore::BLOB_DISPLAY);
    self->view_ = self->display_map_.view();
    self->state_ = self->view_.valid() ? State::Ok : State::Failed;
    self->finalized_ = true;
    return self;
}

std::shared_ptr<NameCardData> NameCardData::load_lastcard(const std::string &path,
                                                          const imgproc::Options &opts) {
    auto &st = cardstore::lastcard();
    if (!st.mount() || !st.available()) return nullptr;

    auto self = std::shared_ptr<NameCardData>(new NameCardData());
    self->path_ = path;
    self->kind_ = Kind::Image;

    uint32_t pdf_len = st.blob_len(cardstore::BLOB_PDF);
    if (pdf_len) {
        self->pdf_.resize(pdf_len);
        if (st.read_blob(cardstore::BLOB_PDF, self->pdf_.data(), pdf_len) && self->adopt_pdf())
            self->kind_ = Kind::Card;
        else
            self->pdf_.clear();
    }

    self->display_map_ = st.map_image(cardstore::BLOB_DISPLAY);
    self->view_ = self->display_map_.view();
    if (!self->view_.valid()) {
        // Display blob missing (write interrupted after the PDF commit): decode
        // the cached PDF's display JPEG synchronously — boot has nothing else to do.
        if (self->kind_ != Kind::Card) return nullptr;
        const nckpdf::Asset *disp = self->card_.find(nckpdf::AssetType::DisplayJpeg);
        if (!disp || uint64_t(disp->offset) + disp->length > self->pdf_.size()) return nullptr;
        if (imgproc::decode_buffer(self->pdf_.data() + disp->offset, disp->length, opts,
                                   self->image_) != imgproc::Status::Ok) return nullptr;
        self->view_ = L8View{self->image_.data, self->image_.w, self->image_.h,
                             static_cast<uint32_t>(self->image_.stride), self->image_.levels};
    }
    self->state_ = State::Ok;
    self->finalized_ = true;
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
            view_ = L8View{image_.data, image_.w, image_.h,
                           static_cast<uint32_t>(image_.stride), image_.levels};
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

std::shared_ptr<SharedCardData> NameCardData::share() const {
    if (kind_ != Kind::Card) return nullptr;
    if (path_.empty()) return SharedCardData::from_store(cardstore::mycard(), card_);
    // Resumed from the lastcard cache (pdf_ populated): the SD file is gone, so
    // read the share PDF from that partition instead of the unmounted path_.
    if (!pdf_.empty()) return SharedCardData::from_store(cardstore::lastcard(), card_);
    return SharedCardData::open(path_);
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

const L8View &NameCardData::display_view() const {
    finalize();
    return view_;
}

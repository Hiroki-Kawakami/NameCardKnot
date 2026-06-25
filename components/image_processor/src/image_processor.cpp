/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "image_processor.hpp"

#include <cstdio>
#include <utility>

#include "alloc.hpp"
#include "pipeline.hpp"
#include "profile.hpp"
#include "rowsource.hpp"
#include "sniff.hpp"
#include "stream.hpp"

namespace imgproc {

const char *status_str(Status s) {
    switch (s) {
        case Status::Ok:                return "Ok";
        case Status::OpenFailed:        return "OpenFailed";
        case Status::UnsupportedFormat: return "UnsupportedFormat";
        case Status::DecodeError:       return "DecodeError";
        case Status::Truncated:         return "Truncated";
        case Status::TooLarge:          return "TooLarge";
        case Status::OutOfMemory:       return "OutOfMemory";
        case Status::BadArgument:       return "BadArgument";
    }
    return "?";
}

// ---- Image ------------------------------------------------------------------

Image::~Image() {
    reset();
}

Image::Image(Image &&other) noexcept {
    *this = std::move(other);
}

Image &Image::operator=(Image &&other) noexcept {
    if (this != &other) {
        reset();
        data   = other.data;
        w      = other.w;
        h      = other.h;
        format = other.format;
        stride = other.stride;
        levels = other.levels;
        other.data = nullptr;
        other.w = other.h = 0;
        other.stride = 0;
    }
    return *this;
}

void Image::reset() {
    img_free(data);
    data = nullptr;
    w = h = 0;
    stride = 0;
}

// ---- Orchestration ----------------------------------------------------------

static Status decode_stream(InputStream &in, const Options &opts, Image &out) {
    out.reset();
    PROF_RESET();
    PROF_T0(t_total);

    uint8_t hdr[InputStream::kPeekMax];
    size_t  n = in.peek(hdr, sizeof hdr);
    Format  fmt = sniff(hdr, n);
    if (fmt == Format::Unknown) return Status::UnsupportedFormat;

    std::unique_ptr<Decoder> dec = make_decoder(fmt);
    if (!dec) return Status::UnsupportedFormat;  // Phase 1: decoders not wired

    Status st = dec->open(in, opts);
    if (st != Status::Ok) return st;

    st = check_src_size(dec->width, dec->height, opts.max_src_pixels);
    if (st != Status::Ok) return st;

    st = run_pipeline(*dec, opts, out);
    PROF_ADD(total_us, t_total);
    PROF_REPORT(out.w, out.h);
    return st;
}

Status decode_file(const char *path, const Options &opts, Image &out) {
    if (!path) return Status::BadArgument;
    FILE *fp = std::fopen(path, "rb");
    if (!fp) return Status::OpenFailed;
    FileInputStream in(fp);
    Status st = in.ok() ? decode_stream(in, opts, out) : Status::OutOfMemory;
    std::fclose(fp);
    return st;
}

Status decode_buffer(const void *data, size_t len, const Options &opts, Image &out) {
    if (!data && len) return Status::BadArgument;
    BufferInputStream in(data, len);
    if (!in.ok()) return Status::OutOfMemory;
    return decode_stream(in, opts, out);
}

}  // namespace imgproc

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "image_processor.hpp"

#include <cstdio>
#include <utility>

#include "pipeline.hpp"
#include "profile.hpp"

#include "imgf_alloc.h"
#include "imgf_decoder.h"
#include "imgf_sniff.h"
#include "imgf_stream.h"

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
        case Status::Cancelled:         return "Cancelled";
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
    imgf_free(data);
    data = nullptr;
    w = h = 0;
    stride = 0;
}

// ---- Orchestration ----------------------------------------------------------

#if IMGPROC_PROFILE
static const char *fmt_name(imgf_format_t f) {
    return f == IMGF_FMT_JPEG ? "jpeg" : f == IMGF_FMT_PNG ? "png" : "?";
}
#endif

// Make the decoder for `fmt`, open it on `s`, and apply the source-size guard.
// The stream source state behind `s` must outlive the returned decoder. Returns
// nullptr with *st set on failure.
static imgf_decoder_t *open_decoder_on_stream(imgf_stream_t s, imgf_format_t fmt,
                                              const Options &opts, Status *st) {
    *st = Status::Ok;
    if (fmt == IMGF_FMT_UNKNOWN) { *st = Status::UnsupportedFormat; return nullptr; }
    imgf_decoder_t *dec = imgf_make_decoder(fmt);
    if (!dec) { *st = Status::UnsupportedFormat; return nullptr; }

    imgf_decode_opts_t dopts = {};
    dopts.target_w = opts.target_w;
    dopts.target_h = opts.target_h;
    dopts.max_src_pixels = opts.max_src_pixels;
    dopts.alloc_caps = opts.alloc_caps;

    imgf_err_t e = imgf_decoder_open(dec, s, &dopts);
    if (e != IMGF_OK) { imgf_decoder_destroy(dec); *st = status_from_imgf(e); return nullptr; }

    uint32_t sw = imgf_decoder_width(dec), sh = imgf_decoder_height(dec);
    if (sw == 0 || sh == 0) { imgf_decoder_destroy(dec); *st = Status::DecodeError; return nullptr; }
    if (opts.max_src_pixels && static_cast<uint64_t>(sw) * sh > opts.max_src_pixels) {
        imgf_decoder_destroy(dec);
        *st = Status::TooLarge;
        return nullptr;
    }
#if IMGPROC_PROFILE
    g_prof.fmt = fmt_name(fmt);
    g_prof.src_w = static_cast<int>(sw);
    g_prof.src_h = static_cast<int>(sh);
#endif
    return dec;
}

// Shared by the synchronous decode_file and the async path (pipeline.cpp). Sniffs
// the container head, then rewinds to `offset`: the imgf file source only seeks
// lazily when offset > 0, so a whole-file decode must undo this peek's advance.
imgf_decoder_t *open_file_decoder(FILE *fp, long offset, size_t length, const Options &opts,
                                  imgf_file_source_t *fs, Status *st) {
    uint8_t hdr[8];
    std::fseek(fp, offset, SEEK_SET);
    size_t n = std::fread(hdr, 1, sizeof hdr, fp);
    imgf_format_t fmt = imgf_sniff(hdr, n);
    std::fseek(fp, offset, SEEK_SET);
    imgf_stream_t s = imgf_stream_from_file(fs, fp, offset, length);
    return open_decoder_on_stream(s, fmt, opts, st);
}

Status decode_file(const char *path, const Options &opts, Image &out, Progress *prog,
                   uint32_t offset, uint32_t length) {
    if (!path) return Status::BadArgument;
    FILE *fp = std::fopen(path, "rb");
    if (!fp) return Status::OpenFailed;

    out.reset();
    PROF_RESET();
    PROF_T0(t_total);

    imgf_file_source_t fs;
    Status st = Status::Ok;
    imgf_decoder_t *dec = open_file_decoder(fp, (long)offset, (size_t)length, opts, &fs, &st);
    if (dec) {
        st = run_pipeline(dec, opts, out, prog);
        imgf_decoder_destroy(dec);
        PROF_ADD(total_us, t_total);
        PROF_REPORT(out.w, out.h);
    }
    std::fclose(fp);
    return st;
}

Status decode_buffer(const void *data, size_t len, const Options &opts, Image &out, Progress *prog) {
    if (!data && len) return Status::BadArgument;
    out.reset();
    PROF_RESET();
    PROF_T0(t_total);

    imgf_format_t fmt = imgf_sniff(data, len < 8 ? len : 8);
    imgf_buffer_source_t bs;
    imgf_stream_t s = imgf_stream_from_buffer(&bs, data, len);
    Status st = Status::Ok;
    imgf_decoder_t *dec = open_decoder_on_stream(s, fmt, opts, &st);
    if (dec) {
        st = run_pipeline(dec, opts, out, prog);
        imgf_decoder_destroy(dec);
        PROF_ADD(total_us, t_total);
        PROF_REPORT(out.w, out.h);
    }
    return st;
}

}  // namespace imgproc

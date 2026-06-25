/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "image_processor.hpp"

#include <cstdlib>
#include <utility>

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
// NOTE: allocation/free of `data` is a stub until Phase 1 introduces the
// caps-aware allocator (heap_caps on device, malloc on host).

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
    std::free(data);
    data = nullptr;
    w = h = 0;
    stride = 0;
}

// ---- Entry points -----------------------------------------------------------
// Phase 0 skeleton: wiring only. Decoders + pipeline arrive in later phases.

Status decode_file(const char *path, const Options &opts, Image &out) {
    (void)path;
    (void)opts;
    out.reset();
    return Status::UnsupportedFormat;
}

Status decode_buffer(const void *data, size_t len, const Options &opts, Image &out) {
    (void)data;
    (void)len;
    (void)opts;
    out.reset();
    return Status::UnsupportedFormat;
}

}  // namespace imgproc

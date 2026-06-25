/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include <new>

#include "png.hpp"
#include "rowsource.hpp"

namespace imgproc {

std::unique_ptr<Decoder> make_decoder(Format fmt) {
    switch (fmt) {
        case Format::Png:   return std::unique_ptr<Decoder>(new (std::nothrow) PngDecoder());
        case Format::Jpeg:  return nullptr;  // Phase 4
        case Format::Unknown: break;
    }
    return nullptr;
}

}  // namespace imgproc

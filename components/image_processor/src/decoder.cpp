/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include <new>

#include "jpeg.hpp"
#include "png.hpp"
#include "rowsource.hpp"

namespace imgproc {

std::unique_ptr<Decoder> make_decoder(Format fmt) {
    switch (fmt) {
        case Format::Png:   return std::unique_ptr<Decoder>(new (std::nothrow) PngDecoder());
        case Format::Jpeg:  return std::unique_ptr<Decoder>(new (std::nothrow) JpegDecoder());
        case Format::Unknown: break;
    }
    return nullptr;
}

}  // namespace imgproc

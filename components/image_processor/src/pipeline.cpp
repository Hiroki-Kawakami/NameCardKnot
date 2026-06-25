/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

#include "alloc.hpp"
#include "color.hpp"
#include "dither.hpp"
#include "pack.hpp"
#include "profile.hpp"

namespace imgproc {

static Status compute_dst(int sw, int sh, const Options &opts, int &dw, int &dh) {
    int tw = opts.target_w, th = opts.target_h;
    if (opts.fit == Fit::Stretch) {
        if (tw <= 0 || th <= 0) return Status::BadArgument;
        dw = tw;
        dh = th;
        return Status::Ok;
    }
    // Contain: scale to fit inside the provided dimension(s), preserve aspect.
    if (tw == 0 && th == 0) {
        dw = sw;
        dh = sh;
        return Status::Ok;
    }
    double rw = tw ? static_cast<double>(tw) / sw : 1e30;
    double rh = th ? static_cast<double>(th) / sh : 1e30;
    double r = std::min(rw, rh);
    dw = std::max(1, static_cast<int>(std::lround(sw * r)));
    dh = std::max(1, static_cast<int>(std::lround(sh * r)));
    return Status::Ok;
}

// Area-average one source intensity row into the destination width.
static void hreduce(const uint16_t *irow, int sw, uint16_t *hrow, int dw) {
    for (int x = 0; x < dw; x++) {
        double fx0 = static_cast<double>(x) * sw / dw;
        double fx1 = static_cast<double>(x + 1) * sw / dw;
        int sx0 = static_cast<int>(fx0);
        int sx1 = static_cast<int>(std::ceil(fx1));
        uint64_t sum = 0, wsum = 0;
        for (int sx = sx0; sx < sx1; sx++) {
            int cx = sx < 0 ? 0 : (sx >= sw ? sw - 1 : sx);
            double lo = std::max(fx0, static_cast<double>(sx));
            double hi = std::min(fx1, static_cast<double>(sx + 1));
            uint64_t wi = static_cast<uint64_t>(std::llround((hi - lo) * 65536.0));
            if (!wi) continue;
            sum += static_cast<uint64_t>(irow[cx]) * wi;
            wsum += wi;
        }
        hrow[x] = wsum ? static_cast<uint16_t>(sum / wsum) : irow[std::min(sx0, sw - 1)];
    }
}

Status run_pipeline(RowSource &src, const Options &opts, Image &out) {
    out.reset();

    int sw = src.width, sh = src.height, ch = src.channels();
    if (sw <= 0 || sh <= 0) return Status::DecodeError;

    int dw, dh;
    Status st = compute_dst(sw, sh, opts, dw, dh);
    if (st != Status::Ok) return st;

    int n = opts.levels < 2 ? 2 : opts.levels;
    if (opts.out == OutFormat::I1 && n != 2) return Status::BadArgument;
    if (opts.out == OutFormat::I4 && n > 16) return Status::BadArgument;

    size_t stride = out_stride(opts.out, dw);
    uint8_t *buf = static_cast<uint8_t *>(img_alloc(stride * dh, opts.alloc_caps));
    if (!buf) return Status::OutOfMemory;

    ColorPipeline color;
    color.init(opts);
    Ditherer dith;
    if (!dith.init(opts, dw)) {
        img_free(buf);
        return Status::OutOfMemory;
    }

    // Per-row working buffers (transient, internal heap).
    std::unique_ptr<uint8_t[]>  srow(new (std::nothrow) uint8_t[static_cast<size_t>(sw) * ch]);
    std::unique_ptr<uint16_t[]> irow(new (std::nothrow) uint16_t[sw]);
    std::unique_ptr<uint16_t[]> hrow(new (std::nothrow) uint16_t[dw]);
    std::unique_ptr<uint64_t[]> vacc(new (std::nothrow) uint64_t[dw]);
    std::unique_ptr<uint8_t[]>  gray(new (std::nothrow) uint8_t[dw]);
    std::unique_ptr<uint8_t[]>  lvls(new (std::nothrow) uint8_t[dw]);
    if (!srow || !irow || !hrow || !vacc || !gray || !lvls) {
        img_free(buf);
        return Status::OutOfMemory;
    }

    auto finalize_row = [&](int y, uint64_t vwsum) {
        PROF_T0(tf);
        for (int x = 0; x < dw; x++) {
            uint32_t avg = static_cast<uint32_t>(vacc[x] / vwsum);
            gray[x] = color.finalize(avg);
        }
        dith.process_row(gray.get(), y, lvls.get());
        pack_row(opts.out, lvls.get(), dw, n, buf + static_cast<size_t>(y) * stride);
        PROF_ADD(dither_us, tf);
    };

    // Streaming vertical box: each source row [sy, sy+1) is distributed by overlap
    // into the destination rows it covers; a row completes when fully spanned.
    std::memset(vacc.get(), 0, sizeof(uint64_t) * dw);
    uint64_t vwsum = 0;
    int y = 0;
    for (int sy = 0; sy < sh && y < dh; sy++) {
        PROF_T0(td);
        bool ok = src.next_row(srow.get());
        PROF_ADD(decode_us, td);
        if (!ok) {
            img_free(buf);
            return Status::Truncated;
        }
        PROF_T0(tt);
        color.to_intensity(srow.get(), sw, ch, irow.get());
        hreduce(irow.get(), sw, hrow.get(), dw);

        double top = sy, bot = sy + 1;
        while (y < dh) {
            double vy0 = static_cast<double>(y) * sh / dh;
            double vy1 = static_cast<double>(y + 1) * sh / dh;
            double w = std::min(bot, vy1) - std::max(top, vy0);
            if (w > 0.0) {
                uint64_t wi = static_cast<uint64_t>(std::llround(w * 65536.0));
                if (!wi) wi = 1;
                for (int x = 0; x < dw; x++) vacc[x] += static_cast<uint64_t>(hrow[x]) * wi;
                vwsum += wi;
            }
            if (vy1 <= bot + 1e-9) {  // dst row y fully covered
                if (vwsum) finalize_row(y, vwsum);
                y++;
                std::memset(vacc.get(), 0, sizeof(uint64_t) * dw);
                vwsum = 0;
            } else {
                break;  // need more source rows for this dst row
            }
        }
        PROF_ADD(transform_us, tt);  // color + downsample (+ dither, subtracted in report)
    }
    if (y < dh && vwsum) {  // flush a residual row left by rounding
        finalize_row(y, vwsum);
        y++;
    }

    out.data = buf;
    out.w = static_cast<uint16_t>(dw);
    out.h = static_cast<uint16_t>(dh);
    out.format = opts.out;
    out.stride = stride;
    out.levels = static_cast<uint8_t>(n);
    return Status::Ok;
}

}  // namespace imgproc

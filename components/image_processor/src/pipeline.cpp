/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

#include "alloc.hpp"
#include "color.hpp"
#include "dither.hpp"
#include "pack.hpp"
#include "profile.hpp"

// Two-stage parallel pipeline: a producer task decodes source rows (the serial
// entropy/IDCT path) on one core while the consumer (the calling thread) runs
// color/downscale/dither/pack on the other. Off (single-threaded) when built
// without FreeRTOS — the host unit tests set -DIMGPROC_PARALLEL=0.
#ifndef IMGPROC_PARALLEL
#define IMGPROC_PARALLEL 1
#endif
#if IMGPROC_PARALLEL
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace imgproc {

static inline bool cancelled(const std::atomic<bool> *c) { return c && c->load(); }

#if IMGPROC_PARALLEL
struct ProducerCtx {
    RowSource *src;
    int sh, slots;
    uint8_t **bufs;
    SemaphoreHandle_t free_sem, filled_sem, done_sem;
    const std::atomic<bool> *cancel;  // shared with the consumer/caller
    volatile bool error;
};

// Decodes source rows into the ring; the consumer drains it. Each g_prof field
// has a single writer (decode/io here, transform/dither on the consumer), so the
// shared struct needs no locking.
static void producer_task(void *arg) {
    ProducerCtx *p = static_cast<ProducerCtx *>(arg);
    for (int sy = 0; sy < p->sh; sy++) {
        if (cancelled(p->cancel)) break;
        xSemaphoreTake(p->free_sem, portMAX_DELAY);
        if (cancelled(p->cancel)) break;  // re-check after waking (consumer may have aborted)
        PROF_T0(td);
        bool ok = p->src->next_row(p->bufs[sy % p->slots]);
        PROF_ADD(decode_us, td);
        if (!ok) {
            p->error = true;
            xSemaphoreGive(p->filled_sem);
            break;
        }
        xSemaphoreGive(p->filled_sem);
    }
    xSemaphoreGive(p->done_sem);
    vTaskDelete(nullptr);
}
#endif

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

// Precomputed horizontal box coverage. The source→dst mapping is identical for
// every row, so the (double) geometry is computed once here; the per-row reduce
// is then integer-only. For each dst x: a contiguous run of source pixels
// [src[x], src[x]+count) with Q16 weights, normalized by wsum[x].
struct HBox {
    std::unique_ptr<int[]>      off;   // dw+1 prefix offsets into w[]
    std::unique_ptr<int[]>      src;   // dw: first source index
    std::unique_ptr<uint32_t[]> w;     // flattened weights (Q16)
    std::unique_ptr<uint32_t[]> wsum;  // dw: total weight per dst x
};

static bool build_hbox(int sw, int dw, HBox &hb) {
    hb.off.reset(new (std::nothrow) int[dw + 1]);
    hb.src.reset(new (std::nothrow) int[dw]);
    hb.wsum.reset(new (std::nothrow) uint32_t[dw]);
    if (!hb.off || !hb.src || !hb.wsum) return false;

    hb.off[0] = 0;
    for (int x = 0; x < dw; x++) {
        double fx0 = static_cast<double>(x) * sw / dw;
        double fx1 = static_cast<double>(x + 1) * sw / dw;
        int sx0 = static_cast<int>(fx0);
        int sx1 = static_cast<int>(std::ceil(fx1));
        if (sx0 < 0) sx0 = 0;
        if (sx1 > sw) sx1 = sw;
        if (sx1 <= sx0) sx1 = sx0 + 1;  // always cover at least one source pixel
        hb.src[x] = sx0;
        hb.off[x + 1] = hb.off[x] + (sx1 - sx0);
    }

    hb.w.reset(new (std::nothrow) uint32_t[hb.off[dw]]);
    if (!hb.w) return false;
    for (int x = 0; x < dw; x++) {
        double fx0 = static_cast<double>(x) * sw / dw;
        double fx1 = static_cast<double>(x + 1) * sw / dw;
        int sx0 = hb.src[x];
        int cnt = hb.off[x + 1] - hb.off[x];
        uint32_t ws = 0;
        for (int i = 0; i < cnt; i++) {
            double lo = std::max(fx0, static_cast<double>(sx0 + i));
            double hi = std::min(fx1, static_cast<double>(sx0 + i + 1));
            uint32_t wi = static_cast<uint32_t>(std::llround((hi - lo) * 65536.0));
            if (!wi) wi = 1;
            hb.w[hb.off[x] + i] = wi;
            ws += wi;
        }
        hb.wsum[x] = ws;
    }
    return true;
}

// Integer area-average of one source intensity row into the destination width.
static void hreduce(const uint16_t *irow, uint16_t *hrow, int dw, const HBox &hb) {
    for (int x = 0; x < dw; x++) {
        const uint32_t *w = hb.w.get() + hb.off[x];
        const uint16_t *p = irow + hb.src[x];
        int cnt = hb.off[x + 1] - hb.off[x];
        uint64_t sum = 0;
        for (int i = 0; i < cnt; i++) sum += static_cast<uint64_t>(p[i]) * w[i];
        hrow[x] = static_cast<uint16_t>(sum / hb.wsum[x]);
    }
}

Status run_pipeline(RowSource &src, const Options &opts, Image &out, Progress *prog) {
    out.reset();

    int sw = src.width, sh = src.height, ch = src.channels();
    if (sw <= 0 || sh <= 0) return Status::DecodeError;

    int dw, dh;
    Status st = compute_dst(sw, sh, opts, dw, dh);
    if (st != Status::Ok) return st;
    if (prog) prog->total.store(dh);
    const std::atomic<bool> *cancel = prog ? &prog->cancel : nullptr;

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

    HBox hb;
    if (!build_hbox(sw, dw, hb)) {
        img_free(buf);
        return Status::OutOfMemory;
    }

    // Vertical box accumulation state (consumer-only; captured by process_row).
    std::memset(vacc.get(), 0, sizeof(uint64_t) * dw);
    uint64_t vwsum = 0;
    int y = 0;

    auto finalize_row = [&](int yy, uint64_t vws) {
        PROF_T0(tf);
        // vws is constant for the row, so divide once and multiply per pixel
        // (a 64-bit divide per pixel was a big chunk of the dither bucket).
        uint64_t inv = (static_cast<uint64_t>(1) << 32) / vws;
        for (int x = 0; x < dw; x++) {
            uint32_t avg = static_cast<uint32_t>((vacc[x] * inv + (1ull << 31)) >> 32);
            if (avg > 65535) avg = 65535;
            gray[x] = color.finalize(avg);
        }
        dith.process_row(gray.get(), yy, lvls.get());
        pack_row(opts.out, lvls.get(), dw, n, buf + static_cast<size_t>(yy) * stride);
        PROF_ADD(dither_us, tf);
    };

    // One source row [sy, sy+1): distribute by vertical overlap into the dst rows
    // it covers; a dst row completes (is finalized) once fully spanned.
    auto process_row = [&](const uint8_t *srow_data, int sy) {
        color.to_intensity(srow_data, sw, ch, irow.get());
        hreduce(irow.get(), hrow.get(), dw, hb);
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
    };

#if IMGPROC_PARALLEL
    {
        const int kSlots = 6;
        std::unique_ptr<uint8_t[]> ring(
            new (std::nothrow) uint8_t[static_cast<size_t>(sw) * ch * kSlots]);
        if (!ring) { img_free(buf); return Status::OutOfMemory; }
        uint8_t *bufs[kSlots];
        for (int i = 0; i < kSlots; i++) bufs[i] = ring.get() + static_cast<size_t>(i) * sw * ch;

        ProducerCtx ctx{};
        ctx.src = &src;
        ctx.sh = sh;
        ctx.slots = kSlots;
        ctx.bufs = bufs;
        ctx.free_sem = xSemaphoreCreateCounting(kSlots, kSlots);
        ctx.filled_sem = xSemaphoreCreateCounting(kSlots, 0);
        ctx.done_sem = xSemaphoreCreateBinary();
        ctx.cancel = cancel;
        ctx.error = false;
        // Pin the decoder to the core NOT running the consumer, so they truly run
        // in parallel instead of time-slicing one core. (Without this the wall-clock
        // stage timings inflate and there is no speedup.)
#ifdef ESP_PLATFORM
        BaseType_t dec_core = 1 - xPortGetCoreID();
#else
        BaseType_t dec_core = tskNO_AFFINITY;  // host: pthreads, core id ignored
#endif
        TaskHandle_t htask = nullptr;
        UBaseType_t prio = uxTaskPriorityGet(nullptr);  // match the consumer's priority
        if (!ctx.free_sem || !ctx.filled_sem || !ctx.done_sem ||
            xTaskCreatePinnedToCore(producer_task, "imgdec", 8192, &ctx, prio, &htask, dec_core) != pdPASS) {
            if (ctx.free_sem) vSemaphoreDelete(ctx.free_sem);
            if (ctx.filled_sem) vSemaphoreDelete(ctx.filled_sem);
            if (ctx.done_sem) vSemaphoreDelete(ctx.done_sem);
            img_free(buf);
            return Status::OutOfMemory;
        }

        bool cancel_hit = false;
        for (int sy = 0; sy < sh; sy++) {
            xSemaphoreTake(ctx.filled_sem, portMAX_DELAY);
            if (ctx.error) break;
            if (cancelled(cancel)) { cancel_hit = true; break; }
            PROF_T0(tt);
            process_row(bufs[sy % kSlots], sy);
            PROF_ADD(transform_us, tt);
            if (prog) prog->done.store(y);
            xSemaphoreGive(ctx.free_sem);
        }
        // Unblock the producer if it is parked on a free slot, then join.
        if (cancel_hit)
            for (int i = 0; i < kSlots; i++) xSemaphoreGive(ctx.free_sem);
        xSemaphoreTake(ctx.done_sem, portMAX_DELAY);
        bool trunc = ctx.error;
        vSemaphoreDelete(ctx.free_sem);
        vSemaphoreDelete(ctx.filled_sem);
        vSemaphoreDelete(ctx.done_sem);
        if (cancel_hit) { img_free(buf); return Status::Cancelled; }
        if (trunc) { img_free(buf); return Status::Truncated; }
    }
#else
    for (int sy = 0; sy < sh && y < dh; sy++) {
        if (cancelled(cancel)) { img_free(buf); return Status::Cancelled; }
        PROF_T0(td);
        bool ok = src.next_row(srow.get());
        PROF_ADD(decode_us, td);
        if (!ok) {
            img_free(buf);
            return Status::Truncated;
        }
        PROF_T0(tt);
        process_row(srow.get(), sy);
        PROF_ADD(transform_us, tt);
        if (prog) prog->done.store(y);
    }
#endif

    if (y < dh && vwsum) {  // flush a residual row left by rounding
        finalize_row(y, vwsum);
        y++;
    }
    if (prog) prog->done.store(dh);  // 100% on success

    out.data = buf;
    out.w = static_cast<uint16_t>(dw);
    out.h = static_cast<uint16_t>(dh);
    out.format = opts.out;
    out.stride = stride;
    out.levels = static_cast<uint8_t>(n);
    return Status::Ok;
}

}  // namespace imgproc

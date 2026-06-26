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

// Streaming downscale -> color -> dither -> pack pipeline. `run_pipeline` runs it
// on the calling task; with a ParallelCfg the decode (producer) and color+dither
// (consumer) halves split across two tasks, connected by a source-row ring (see
// run_pipeline_dual). FreeRTOS comes from ESP-IDF on device / idf_compat on the
// simulator; the host unit tests build IMGPROC_ASYNC=0 and run serially.
#ifndef IMGPROC_ASYNC
#define IMGPROC_ASYNC 1
#endif
#if IMGPROC_ASYNC
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace imgproc {

static inline bool cancelled(const std::atomic<bool> *c) { return c && c->load(); }

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

// The consumer half: everything downstream of the decoder. It owns the output
// buffer + per-row scratch and turns a stream of source rows (fed in order via
// consume_row) into the dithered, packed image. Serial and dual paths share it;
// the producer (the decoder) lives outside so it can run on its own task.
struct PipelineCore {
    const Options *opts = nullptr;
    Progress *prog = nullptr;

    int sw = 0, sh = 0, ch = 0, dw = 0, dh = 0, n = 0;
    size_t stride = 0;
    uint8_t *buf = nullptr;

    ColorPipeline color;
    Ditherer dith;
    HBox hb;
    std::unique_ptr<uint16_t[]> irow;
    std::unique_ptr<uint16_t[]> hrow;
    std::unique_ptr<uint64_t[]> vacc;
    std::unique_ptr<uint8_t[]>  gray;
    std::unique_ptr<uint8_t[]>  lvls;

    uint64_t vwsum = 0;
    int y = 0;  // next dst row to finalize; the pipeline is done at y == dh

    Status init(const RowSource &src, const Options &o, Progress *p);
    void   consume_row(const uint8_t *srow_data, int sy);
    Status finish(Image &out);              // flush residual, hand buf to out
    void   discard() { img_free(buf); buf = nullptr; }

  private:
    void finalize_row(int yy, uint64_t vws);
};

Status PipelineCore::init(const RowSource &src, const Options &o, Progress *p) {
    opts = &o;
    prog = p;
    sw = src.width;
    sh = src.height;
    ch = src.channels();
    if (sw <= 0 || sh <= 0) return Status::DecodeError;

    Status st = compute_dst(sw, sh, o, dw, dh);
    if (st != Status::Ok) return st;
    if (prog) prog->total.store(dh);

    n = o.levels < 2 ? 2 : o.levels;
    if (o.out == OutFormat::I1 && n != 2) return Status::BadArgument;
    if (o.out == OutFormat::I4 && n > 16) return Status::BadArgument;

    stride = out_stride(o.out, dw);
    buf = static_cast<uint8_t *>(img_alloc(stride * dh, o.alloc_caps));
    if (!buf) return Status::OutOfMemory;

    color.init(o);
    if (!dith.init(o, dw)) { discard(); return Status::OutOfMemory; }

    // Per-row working buffers (transient, internal heap).
    irow.reset(new (std::nothrow) uint16_t[sw]);
    hrow.reset(new (std::nothrow) uint16_t[dw]);
    vacc.reset(new (std::nothrow) uint64_t[dw]);
    gray.reset(new (std::nothrow) uint8_t[dw]);
    lvls.reset(new (std::nothrow) uint8_t[dw]);
    if (!irow || !hrow || !vacc || !gray || !lvls) { discard(); return Status::OutOfMemory; }
    if (!build_hbox(sw, dw, hb)) { discard(); return Status::OutOfMemory; }

    std::memset(vacc.get(), 0, sizeof(uint64_t) * dw);
    return Status::Ok;
}

void PipelineCore::finalize_row(int yy, uint64_t vws) {
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
    pack_row(opts->out, lvls.get(), dw, n, buf + static_cast<size_t>(yy) * stride);
    PROF_ADD(dither_us, tf);
}

// One source row [sy, sy+1): distribute by vertical overlap into the dst rows it
// covers; a dst row completes (is finalized) once fully spanned. Rows must arrive
// in order. (transform_us is the color+down bucket; it includes dither_us.)
void PipelineCore::consume_row(const uint8_t *srow_data, int sy) {
    PROF_T0(tt);
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
    PROF_ADD(transform_us, tt);
}

Status PipelineCore::finish(Image &out) {
    if (y < dh && vwsum) {  // flush a residual row left by rounding
        finalize_row(y, vwsum);
        y++;
    }
    if (prog) prog->done.store(dh);  // 100% on success
    out.data = buf;
    out.w = static_cast<uint16_t>(dw);
    out.h = static_cast<uint16_t>(dh);
    out.format = opts->out;
    out.stride = stride;
    out.levels = static_cast<uint8_t>(n);
    buf = nullptr;  // ownership transferred to out
    return Status::Ok;
}

// Decode + consume on one task.
static Status run_pipeline_serial(RowSource &src, const Options &opts, Image &out, Progress *prog) {
    out.reset();
    PipelineCore core;
    Status st = core.init(src, opts, prog);
    if (st != Status::Ok) return st;

    const std::atomic<bool> *cancel = prog ? &prog->cancel : nullptr;
    std::unique_ptr<uint8_t[]> srow(
        new (std::nothrow) uint8_t[static_cast<size_t>(core.sw) * core.ch]);
    if (!srow) { core.discard(); return Status::OutOfMemory; }

    for (int sy = 0; sy < core.sh && core.y < core.dh; sy++) {
        if (cancelled(cancel)) { core.discard(); return Status::Cancelled; }
        PROF_T0(td);
        bool ok = src.next_row(srow.get());
        PROF_ADD(decode_us, td);
        if (!ok) { core.discard(); return Status::Truncated; }
        core.consume_row(srow.get(), sy);
        if (prog) prog->done.store(core.y);
    }
    return core.finish(out);
}

#if IMGPROC_ASYNC
// --- Dual-core split -------------------------------------------------------
// The decode (producer) writes each source row into a free slot of a ring; the
// consumer task pulls slots in order and runs color+down+dither. Two counting
// semaphores form the bounded buffer; a slot tag carries the source row index or
// a terminal marker (-1 end, -2 decode error). Each profiling field has a single
// writer (producer: decode/io/entropy/idct/post; consumer: transform/dither), so
// the overlapping stages don't race g_prof.

static constexpr int kSentinelEnd = -1;
static constexpr int kSentinelErr = -2;

struct RowChan {
    int    nslots = 0;
    size_t row_bytes = 0;
    uint8_t *slots = nullptr;
    std::unique_ptr<int[]> tag;
    SemaphoreHandle_t free_sem = nullptr;    // counts empty slots
    SemaphoreHandle_t filled_sem = nullptr;  // counts ready slots
};

struct DualCtx {
    PipelineCore *core = nullptr;
    RowChan *chan = nullptr;
    Image *out = nullptr;
    Progress *prog = nullptr;
    std::atomic<bool> consumer_done{false};  // y reached dh: producer may stop
    std::atomic<bool> prod_failed{false};    // a next_row() returned false
    Status status = Status::Ok;
    SemaphoreHandle_t done_sem = nullptr;
};

static void consume_task(void *arg) {
    DualCtx *ctx = static_cast<DualCtx *>(arg);
    RowChan *ch = ctx->chan;
    PipelineCore *core = ctx->core;
    const std::atomic<bool> *cancel = ctx->prog ? &ctx->prog->cancel : nullptr;

    int rd = 0;
    for (;;) {
        xSemaphoreTake(ch->filled_sem, portMAX_DELAY);
        int tag = ch->tag[rd];
        if (tag == kSentinelEnd) break;
        if (tag == kSentinelErr) { ctx->prod_failed.store(true); break; }
        // Keep draining (recycling slots) even past dh so the producer never
        // blocks; only stop *processing* once the image is complete or cancelled.
        if (core->y < core->dh && !cancelled(cancel)) {
            core->consume_row(ch->slots + static_cast<size_t>(rd) * ch->row_bytes, tag);
            if (ctx->prog) ctx->prog->done.store(core->y);
            if (core->y >= core->dh) ctx->consumer_done.store(true);
        }
        xSemaphoreGive(ch->free_sem);
        rd = (rd + 1) % ch->nslots;
    }

    if (cancelled(cancel)) { ctx->status = Status::Cancelled; core->discard(); }
    else if (ctx->prod_failed.load()) { ctx->status = Status::Truncated; core->discard(); }
    else ctx->status = core->finish(*ctx->out);
    xSemaphoreGive(ctx->done_sem);
    vTaskDelete(nullptr);
}

// Runs on the calling task: decode each row into a ring slot for the consumer.
static void produce_rows(RowSource &src, DualCtx &ctx) {
    RowChan &ch = *ctx.chan;
    const std::atomic<bool> *cancel = ctx.prog ? &ctx.prog->cancel : nullptr;
    int wr = 0;
    for (int sy = 0; sy < ctx.core->sh; sy++) {
        if (cancelled(cancel) || ctx.consumer_done.load()) break;
        xSemaphoreTake(ch.free_sem, portMAX_DELAY);
        uint8_t *slot = ch.slots + static_cast<size_t>(wr) * ch.row_bytes;
        PROF_T0(td);
        bool ok = src.next_row(slot);
        PROF_ADD(decode_us, td);
        ch.tag[wr] = ok ? sy : kSentinelErr;
        xSemaphoreGive(ch.filled_sem);
        wr = (wr + 1) % ch.nslots;
        if (!ok) return;  // the -2 tag is the consumer's terminal signal
    }
    xSemaphoreTake(ch.free_sem, portMAX_DELAY);
    ch.tag[wr] = kSentinelEnd;
    xSemaphoreGive(ch.filled_sem);
}

static Status run_pipeline_dual(RowSource &src, const Options &opts, Image &out, Progress *prog,
                                const ParallelCfg &par) {
    out.reset();

    PipelineCore core;
    Status st = core.init(src, opts, prog);
    if (st != Status::Ok) return st;

    RowChan ch;
    ch.row_bytes = static_cast<size_t>(core.sw) * core.ch;
    int want = ch.row_bytes ? static_cast<int>(48u * 1024 / ch.row_bytes) : 0;
    ch.nslots = want < 4 ? 4 : (want > 32 ? 32 : want);
    size_t ring_bytes = ch.row_bytes * ch.nslots;
    // Internal RAM keeps the decode off the PSRAM bus; fall back to PSRAM if tight.
    ch.slots = static_cast<uint8_t *>(img_alloc_internal(ring_bytes));
    if (!ch.slots) ch.slots = static_cast<uint8_t *>(img_alloc(ring_bytes, 0));
    ch.tag.reset(new (std::nothrow) int[ch.nslots]);
    ch.free_sem = xSemaphoreCreateCounting(ch.nslots, ch.nslots);
    ch.filled_sem = xSemaphoreCreateCounting(ch.nslots, 0);

    DualCtx ctx;
    ctx.core = &core;
    ctx.chan = &ch;
    ctx.out = &out;
    ctx.prog = prog;
    ctx.done_sem = xSemaphoreCreateBinary();

    TaskHandle_t h = nullptr;
    bool ready = ch.slots && ch.tag && ch.free_sem && ch.filled_sem && ctx.done_sem &&
                 xTaskCreatePinnedToCore(consume_task, "imgcons", 4096, &ctx,
                                         static_cast<UBaseType_t>(par.consumer_prio), &h,
                                         static_cast<BaseType_t>(par.consumer_core)) == pdPASS;

    Status result;
    if (ready) {
        produce_rows(src, ctx);
        xSemaphoreTake(ctx.done_sem, portMAX_DELAY);
        result = ctx.status;
    }

    if (ch.free_sem) vSemaphoreDelete(ch.free_sem);
    if (ch.filled_sem) vSemaphoreDelete(ch.filled_sem);
    if (ctx.done_sem) vSemaphoreDelete(ctx.done_sem);
    if (ch.slots) img_free(ch.slots);

    // Couldn't stand up the second task → nothing consumed from src yet, so just
    // run the whole pipeline serially on this task.
    if (!ready) {
        core.discard();
        return run_pipeline_serial(src, opts, out, prog);
    }
    return result;
}
#endif  // IMGPROC_ASYNC

Status run_pipeline(RowSource &src, const Options &opts, Image &out, Progress *prog,
                    const ParallelCfg *par) {
#if IMGPROC_ASYNC
    if (par) return run_pipeline_dual(src, opts, out, prog, *par);
#else
    (void)par;
#endif
    return run_pipeline_serial(src, opts, out, prog);
}

}  // namespace imgproc

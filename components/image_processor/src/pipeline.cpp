/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "pipeline.hpp"

#include <atomic>
#include <new>
#include <utility>

#include "profile.hpp"

#include "imgf_alloc.h"
#include "imgf_dither.h"
#include "imgf_raw_encoder.h"
#include "imgf_recolor.h"
#include "imgf_resize.h"

// The pipeline is pure orchestration over image_framework: it maps Options to
// the imgf module options and streams the opened decoder through recolor (front)
// -> imgf resizer (Gray8 downscale) -> recolor (finalize) -> imgf dither -> imgf
// raw encoder (pack). The async DecodeJob path runs the decode (producer) and the
// color halves (consumer) on two tasks via imgf_async. Host unit tests build
// IMGPROC_ASYNC=0 and only use the serial path.
#ifndef IMGPROC_ASYNC
#define IMGPROC_ASYNC 1
#endif
#if IMGPROC_ASYNC
#include "imgf_async.h"
#endif

namespace imgproc {

static inline bool cancelled(const std::atomic<bool> *c) { return c && c->load(); }

Status status_from_imgf(imgf_err_t e) {
    switch (e) {
        case IMGF_OK:                return Status::Ok;
        case IMGF_ERR_TRUNCATED:     return Status::Truncated;
        case IMGF_ERR_DECODE:        return Status::DecodeError;
        case IMGF_ERR_UNSUPPORTED:   return Status::UnsupportedFormat;
        case IMGF_ERR_OOM:           return Status::OutOfMemory;
        case IMGF_ERR_TOO_LARGE:     return Status::TooLarge;
        case IMGF_ERR_INVALID_ARG:   return Status::BadArgument;
        case IMGF_ERR_INVALID_STATE: default: return Status::DecodeError;
    }
}

// ---- Options -> imgf module options ----------------------------------------

static imgf_recolor_opts_t recolor_opts(const Options &o) {
    imgf_recolor_opts_t r = {};
    switch (o.luma) {
        case Luma::Rec601:  r.luma = IMGF_LUMA_REC601; break;
        case Luma::Average: r.luma = IMGF_LUMA_AVERAGE; break;
        case Luma::Custom:  r.luma = IMGF_LUMA_CUSTOM; break;
        case Luma::Rec709:  default: r.luma = IMGF_LUMA_REC709; break;
    }
    r.luma_custom[0] = o.luma_custom[0];
    r.luma_custom[1] = o.luma_custom[1];
    r.luma_custom[2] = o.luma_custom[2];
    switch (o.in_gamma) {
        case GammaIn::Power:  r.in_gamma = IMGF_GAMMA_IN_POWER; break;
        case GammaIn::Linear: r.in_gamma = IMGF_GAMMA_IN_LINEAR; break;
        case GammaIn::Srgb:   default: r.in_gamma = IMGF_GAMMA_IN_SRGB; break;
    }
    r.in_power = o.in_power;
    switch (o.out_gamma) {
        case GammaOut::Power:  r.out_gamma = IMGF_GAMMA_OUT_POWER; break;
        case GammaOut::EpdLut: r.out_gamma = IMGF_GAMMA_OUT_EPDLUT; break;
        case GammaOut::Srgb:   default: r.out_gamma = IMGF_GAMMA_OUT_SRGB; break;
    }
    r.out_power = o.out_power;
    r.epd_lut16 = o.epd_lut16;
    r.linear_downsample = o.linear_downsample;
    r.invert = o.invert;
    return r;
}

static imgf_dither_algo_t dither_algo(Dither d) {
    switch (d) {
        case Dither::Bayer2:         return IMGF_DITHER_BAYER2;
        case Dither::Bayer4:         return IMGF_DITHER_BAYER4;
        case Dither::Bayer8:         return IMGF_DITHER_BAYER8;
        case Dither::FloydSteinberg: return IMGF_DITHER_FLOYD_STEINBERG;
        case Dither::Atkinson:       return IMGF_DITHER_ATKINSON;
        case Dither::Sierra:         return IMGF_DITHER_SIERRA;
        case Dither::JJN:            return IMGF_DITHER_JJN;
        case Dither::Stucki:         return IMGF_DITHER_STUCKI;
        case Dither::None:           default: return IMGF_DITHER_NONE;
    }
}

static imgf_raw_format_t raw_format(OutFormat f) {
    switch (f) {
        case OutFormat::I4: return IMGF_RAW_I4;
        case OutFormat::I1: return IMGF_RAW_I1;
        case OutFormat::L8: default: return IMGF_RAW_L8;
    }
}

// ---- Consumer: everything downstream of the decoder -------------------------
// Owns the output buffer + imgf modules and turns a stream of source rows (fed
// in order via consume_source_row) into the dithered, packed image. Serial and
// async paths share it; the decoder (producer) lives outside so it can run on its
// own task. recolor straddles the resizer so the box average stays in linear
// light (the resizer is geometry-only, Gray8->Gray8).
struct Consumer {
    const Options *opts = nullptr;
    Progress *prog = nullptr;
    imgf_pixfmt_t src_pf = IMGF_PIX_GRAY8;
    int sw = 0, sh = 0, dw = 0, dh = 0, n = 0;
    size_t stride = 0;
    uint8_t *buf = nullptr;

    imgf_recolor_t *rc = nullptr;
    imgf_resizer_t *rz = nullptr;
    imgf_dither_t  *dt = nullptr;
    imgf_encoder_t *enc = nullptr;
    uint8_t *inten = nullptr;  // max(sw,dw): to_intensity staging + resized-row scratch
    uint8_t *dith = nullptr;   // dw dithered row
    int out_row = 0;

    Status init(imgf_decoder_t *dec, const Options &o, Progress *p);
    Status consume_source_row(const uint8_t *src_row);
    Status finish(Image &out);     // flush residual + hand buf to out (no free)
    void   free_modules();         // free modules + scratch, keep buf
    void   destroy();              // free_modules + buf

  private:
    Status emit_row();             // pop resized -> finalize -> dither -> pack
};

Status Consumer::init(imgf_decoder_t *dec, const Options &o, Progress *p) {
    opts = &o;
    prog = p;
    sw = imgf_decoder_width(dec);
    sh = imgf_decoder_height(dec);
    src_pf = imgf_decoder_pixfmt(dec);
    if (sw <= 0 || sh <= 0) return Status::DecodeError;

    n = o.levels < 2 ? 2 : o.levels;
    if (o.out == OutFormat::I1 && n != 2) return Status::BadArgument;
    if (o.out == OutFormat::I4 && n > 16) return Status::BadArgument;

    imgf_resize_opts_t ro = {};
    ro.target_w = o.target_w;
    ro.target_h = o.target_h;
    ro.fit = (o.fit == Fit::Stretch) ? IMGF_FIT_STRETCH : IMGF_FIT_CONTAIN;
    ro.dst_pixfmt = IMGF_PIX_GRAY8;
    ro.alloc_caps = o.alloc_caps;

    uint16_t dw16 = 0, dh16 = 0;
    imgf_err_t e = imgf_resize_compute_dst(sw, sh, &ro, &dw16, &dh16);
    if (e != IMGF_OK) return status_from_imgf(e);
    dw = dw16;
    dh = dh16;
    if (prog) prog->total.store(dh);

    imgf_raw_format_t rf = raw_format(o.out);
    stride = imgf_raw_format_stride(rf, dw);
    buf = static_cast<uint8_t *>(imgf_alloc(stride * dh, o.alloc_caps));
    if (!buf) return Status::OutOfMemory;

    imgf_recolor_opts_t reo = recolor_opts(o);
    rc = imgf_recolor_create(&reo, &e);
    if (!rc) { destroy(); return status_from_imgf(e); }

    rz = imgf_resizer_create(sw, sh, IMGF_PIX_GRAY8, &ro, &e);
    if (!rz) { destroy(); return status_from_imgf(e); }

    imgf_dither_opts_t doo = {};
    doo.algo = dither_algo(o.dither);
    doo.levels = static_cast<uint8_t>(n);
    doo.serpentine = o.serpentine;
    doo.out_mode = (o.out == OutFormat::L8) ? IMGF_DITHER_OUT_GRAY : IMGF_DITHER_OUT_INDEX;
    doo.alloc_caps = o.alloc_caps;
    dt = imgf_dither_create(static_cast<uint16_t>(dw), &doo, &e);
    if (!dt) { destroy(); return status_from_imgf(e); }

    imgf_raw_encoder_opts_t eo = {};
    eo.alloc_caps = o.alloc_caps;
    enc = imgf_raw_encoder_create(rf, static_cast<uint16_t>(dw), static_cast<uint16_t>(dh), &eo, &e);
    if (!enc) { destroy(); return status_from_imgf(e); }
    e = imgf_encoder_bind_buffer(enc, buf, stride * dh);
    if (e != IMGF_OK) { destroy(); return status_from_imgf(e); }

    int wide = sw > dw ? sw : dw;
    inten = static_cast<uint8_t *>(imgf_alloc_internal(wide));
    dith = static_cast<uint8_t *>(imgf_alloc_internal(dw));
    if (!inten || !dith) { destroy(); return Status::OutOfMemory; }
    return Status::Ok;
}

Status Consumer::emit_row() {
    if (!imgf_resizer_pop_row(rz, inten)) return Status::Ok;  // nothing ready
    PROF_T0(tf);
    imgf_recolor_finalize(rc, inten, dw, inten);
    if (imgf_dither_push_row(dt, inten) < 0) return status_from_imgf(imgf_dither_last_error(dt));
    imgf_dither_pop_row(dt, dith);
    if (imgf_encoder_push_row(enc, dith) < 0) return status_from_imgf(imgf_encoder_last_error(enc));
    PROF_ADD(dither_us, tf);
    out_row++;
    if (prog) prog->done.store(out_row);
    return Status::Ok;
}

Status Consumer::consume_source_row(const uint8_t *src_row) {
    PROF_T0(tt);
    imgf_recolor_to_intensity(rc, src_row, sw, src_pf, inten);
    int ready = imgf_resizer_push_row(rz, inten);
    if (ready < 0) { PROF_ADD(transform_us, tt); return status_from_imgf(imgf_resizer_last_error(rz)); }
    Status st = Status::Ok;
    for (int i = 0; i < ready && out_row < dh && st == Status::Ok; i++) st = emit_row();
    PROF_ADD(transform_us, tt);
    return st;
}

Status Consumer::finish(Image &out) {
    if (imgf_resizer_finish(rz) > 0 && out_row < dh) {
        Status st = emit_row();
        if (st != Status::Ok) return st;
    }
    if (out_row != dh) return Status::DecodeError;
    imgf_err_t e = imgf_encoder_finish(enc, nullptr);
    if (e != IMGF_OK) return status_from_imgf(e);

    out.data = buf;
    out.w = static_cast<uint16_t>(dw);
    out.h = static_cast<uint16_t>(dh);
    out.format = opts->out;
    out.stride = stride;
    out.levels = static_cast<uint8_t>(n);
    buf = nullptr;  // ownership transferred to out
    if (prog) prog->done.store(dh);
    return Status::Ok;
}

void Consumer::free_modules() {
    if (enc) { imgf_encoder_destroy(enc); enc = nullptr; }
    if (dt) { imgf_dither_destroy(dt); dt = nullptr; }
    if (rz) { imgf_resizer_destroy(rz); rz = nullptr; }
    if (rc) { imgf_recolor_destroy(rc); rc = nullptr; }
    imgf_free(inten); inten = nullptr;
    imgf_free(dith); dith = nullptr;
}

void Consumer::destroy() {
    free_modules();
    imgf_free(buf);
    buf = nullptr;
}

// ---- Serial -----------------------------------------------------------------

Status run_pipeline(imgf_decoder_t *dec, const Options &opts, Image &out, Progress *prog) {
    out.reset();
    Consumer c;
    Status st = c.init(dec, opts, prog);
    if (st != Status::Ok) return st;

    const std::atomic<bool> *cancel = prog ? &prog->cancel : nullptr;
    size_t row_bytes = static_cast<size_t>(c.sw) * imgf_pixfmt_bpp(c.src_pf);
    uint8_t *src_row = static_cast<uint8_t *>(imgf_alloc_internal(row_bytes));
    if (!src_row) { c.destroy(); return Status::OutOfMemory; }

    Status err = Status::Ok;
    for (int sy = 0; sy < c.sh && c.out_row < c.dh; sy++) {
        if (cancelled(cancel)) { err = Status::Cancelled; break; }
        PROF_T0(td);
        bool ok = imgf_decoder_next_row(dec, src_row);
        PROF_ADD(decode_us, td);
        if (!ok) {
            imgf_err_t de = imgf_decoder_last_error(dec);
            err = de != IMGF_OK ? status_from_imgf(de) : Status::Truncated;
            break;
        }
        err = c.consume_source_row(src_row);
        if (err != Status::Ok) break;
    }
    imgf_free(src_row);

    if (err != Status::Ok) { c.destroy(); return err; }
    st = c.finish(out);
    if (st == Status::Ok) { c.free_modules(); return Status::Ok; }
    c.destroy();
    return st;
}

// ---- Dual-core async (imgf_async) ------------------------------------------
#if IMGPROC_ASYNC

struct ProdCtx {
    imgf_decoder_t *dec;
    int sh;
    uint8_t *row;
    const std::atomic<bool> *cancel;
    Status status;
};

struct ConsCtx {
    Consumer *c;
    Image *out;
    const std::atomic<bool> *cancel;
    Status status;
};

static imgf_err_t producer_cb(void *user, imgf_async_ring_t *ring) {
    ProdCtx *p = static_cast<ProdCtx *>(user);
    for (int sy = 0; sy < p->sh; sy++) {
        if (cancelled(p->cancel)) {
            p->status = Status::Cancelled;
            imgf_async_ring_abort(ring);
            return IMGF_ERR_INVALID_STATE;
        }
        PROF_T0(td);
        bool ok = imgf_decoder_next_row(p->dec, p->row);
        PROF_ADD(decode_us, td);
        if (!ok) {
            imgf_err_t de = imgf_decoder_last_error(p->dec);
            p->status = de != IMGF_OK ? status_from_imgf(de) : Status::Truncated;
            imgf_async_ring_abort(ring);
            return de != IMGF_OK ? de : IMGF_ERR_TRUNCATED;
        }
        imgf_err_t pe = imgf_async_ring_put(ring, p->row);
        if (pe != IMGF_OK) return pe;  // consumer done/aborted — stop cleanly
    }
    return IMGF_OK;
}

static imgf_err_t consumer_cb(void *user, imgf_async_ring_t *ring) {
    ConsCtx *cc = static_cast<ConsCtx *>(user);
    Consumer *c = cc->c;
    uint8_t *row = static_cast<uint8_t *>(imgf_alloc_internal(imgf_async_ring_row_bytes(ring)));
    if (!row) { cc->status = Status::OutOfMemory; imgf_async_ring_abort(ring); return IMGF_ERR_OOM; }

    Status err = Status::Ok;
    for (;;) {
        int r = imgf_async_ring_get(ring, row);
        if (r == 0) break;
        if (r < 0) { err = Status::Truncated; break; }
        if (cancelled(cc->cancel)) { err = Status::Cancelled; imgf_async_ring_abort(ring); break; }
        // Keep draining past dh so the producer never blocks; only process while
        // the image is incomplete.
        if (c->out_row < c->dh) {
            err = c->consume_source_row(row);
            if (err != Status::Ok) { imgf_async_ring_abort(ring); break; }
        }
    }
    imgf_free(row);

    if (err == Status::Ok) err = c->finish(*cc->out);
    cc->status = err;
    return err == Status::Ok ? IMGF_OK
           : err == Status::OutOfMemory ? IMGF_ERR_OOM
                                        : IMGF_ERR_DECODE;
}

// Heap-owned state for one background decode: it outlives async_decode_start (the
// two tasks reference it) until async_decode_join reclaims it.
struct AsyncDecode {
    FILE *fp = nullptr;
    imgf_file_source_t fs = {};
    imgf_decoder_t *dec = nullptr;
    Consumer consumer;
    ProdCtx prod = {};
    ConsCtx cons = {};
    uint8_t *prod_row = nullptr;
    Image result;
    Progress *prog = nullptr;
    imgf_async_job_t *job = nullptr;
};

AsyncDecode *async_decode_start(const char *path, const Options &opts, Progress *prog,
                                const ParallelCfg &par, uint32_t offset, uint32_t length,
                                Status *out_status) {
    *out_status = Status::Ok;
    if (!path) { *out_status = Status::BadArgument; return nullptr; }

    AsyncDecode *a = new (std::nothrow) AsyncDecode();
    if (!a) { *out_status = Status::OutOfMemory; return nullptr; }
    a->prog = prog;

    a->fp = std::fopen(path, "rb");
    if (!a->fp) { *out_status = Status::OpenFailed; delete a; return nullptr; }

    Status st = Status::Ok;
    a->dec = open_file_decoder(a->fp, (long)offset, (size_t)length, opts, &a->fs, &st);
    if (!a->dec) { *out_status = st; std::fclose(a->fp); delete a; return nullptr; }

    st = a->consumer.init(a->dec, opts, prog);
    if (st != Status::Ok) {
        *out_status = st;
        imgf_decoder_destroy(a->dec);
        std::fclose(a->fp);
        delete a;
        return nullptr;
    }

    size_t row_bytes = static_cast<size_t>(a->consumer.sw) * imgf_pixfmt_bpp(a->consumer.src_pf);
    a->prod_row = static_cast<uint8_t *>(imgf_alloc_internal(row_bytes));
    if (!a->prod_row) {
        *out_status = Status::OutOfMemory;
        a->consumer.destroy();
        imgf_decoder_destroy(a->dec);
        std::fclose(a->fp);
        delete a;
        return nullptr;
    }

    const std::atomic<bool> *cancel = prog ? &prog->cancel : nullptr;
    a->prod = ProdCtx{a->dec, a->consumer.sh, a->prod_row, cancel, Status::Ok};
    a->cons = ConsCtx{&a->consumer, &a->result, cancel, Status::Ok};

    imgf_async_opts_t ao = {};
    ao.producer_core = par.producer_core;
    ao.producer_prio = par.producer_prio;
    ao.producer_stack_words = 4096;
    ao.consumer_core = par.consumer_core;
    ao.consumer_prio = par.consumer_prio;
    ao.consumer_stack_words = 4096;
    ao.row_bytes = row_bytes;
    int want = row_bytes ? static_cast<int>(48u * 1024 / row_bytes) : 0;
    ao.ring_slots = want < 4 ? 4 : (want > 32 ? 32 : want);
    ao.alloc_caps = opts.alloc_caps;

    a->job = imgf_async_start(&ao, producer_cb, &a->prod, consumer_cb, &a->cons);
    if (!a->job) {
        // Couldn't spawn the tasks; nothing consumed yet. Tear down and tell the
        // caller (out_status == Ok) to decode synchronously instead.
        imgf_free(a->prod_row);
        a->consumer.destroy();
        imgf_decoder_destroy(a->dec);
        std::fclose(a->fp);
        delete a;
        *out_status = Status::Ok;
        return nullptr;
    }
    return a;
}

bool async_decode_done(const AsyncDecode *a) { return imgf_async_job_done(a->job); }

Status async_decode_join(AsyncDecode *a, Image &out) {
    imgf_err_t ae = imgf_async_job_join(a->job);
    const std::atomic<bool> *cancel = a->prog ? &a->prog->cancel : nullptr;

    Status result;
    if (cancelled(cancel)) result = Status::Cancelled;
    else if (a->prod.status != Status::Ok && a->prod.status != Status::Cancelled) result = a->prod.status;
    else if (a->cons.status != Status::Ok) result = a->cons.status;
    else result = status_from_imgf(ae);

    if (result == Status::Ok) {
        out = std::move(a->result);   // consumer_cb's finish() filled result
        a->consumer.free_modules();
    } else {
        a->consumer.destroy();        // frees the output buffer if finish didn't transfer it
    }
    imgf_free(a->prod_row);
    imgf_decoder_destroy(a->dec);
    std::fclose(a->fp);
    delete a;
    return result;
}

#endif  // IMGPROC_ASYNC

}  // namespace imgproc

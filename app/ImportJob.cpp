/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ImportJob.hpp"
#include "CardStore.hpp"
#include "NameRaster.hpp"
#include "namecard_pdf.hpp"
#include "lvgl.hpp"  // lv_lock/lv_unlock
#include <cstdio>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = n > 0;
    if (ok) {
        out.resize((size_t)n);
        ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    }
    fclose(f);
    return ok;
}

cardstore::Blob image_meta(const imgproc::Image &img) {
    cardstore::Blob b{};
    b.w = img.w;
    b.h = img.h;
    b.stride = (uint32_t)img.stride;
    b.levels = img.levels;
    b.format = cardstore::FMT_L8;
    return b;
}

}  // namespace

void ImportJob::set_phase(int base, int span) {
    prog_.done.store(0);
    prog_.total.store(0);
    phase_base_.store(base);
    phase_span_.store(span);
}

int ImportJob::progress_pct() const {
    int base = phase_base_.load(), span = phase_span_.load();
    if (span <= 0) return base;
    int t = prog_.total.load(), d = prog_.done.load();
    int within = t > 0 ? (d >= t ? 100 : d * 100 / t) : 0;
    return base + span * within / 100;
}

void ImportJob::cancel() {
    prog_.cancel.store(true);
}

void ImportJob::run() {
    auto fail = [&] { state_.store(State::Failed); };
    auto cancelled = [&] { return prog_.cancel.load(); };

    std::vector<uint8_t> pdf;
    if (!read_file(path_, pdf)) return fail();
    if (cancelled()) { state_.store(State::Cancelled); return; }

    nckpdf::Card card;
    if (nckpdf::parse_buffer(pdf.data(), pdf.size(), card) != nckpdf::Status::Ok) return fail();
    const nckpdf::Asset *disp = card.find(nckpdf::AssetType::DisplayJpeg);
    if (!disp) return fail();  // not a viewable .mnc.pdf
    const nckpdf::Asset *glyph_a = card.find(nckpdf::AssetType::NameGlyphs);

    cardstore::Store::Writer w(cardstore::mycard());
    if (!w.begin()) return fail();

    // (1) Full PDF.
    set_phase(5, 0);
    if (!w.write_blob(cardstore::BLOB_PDF, pdf.data(), (uint32_t)pdf.size())) { w.abort(); return fail(); }
    if (cancelled()) { w.abort(); state_.store(State::Cancelled); return; }

    auto decode_to_blob = [&](cardstore::BlobId id, uint16_t tw, uint16_t th, int base, int span) -> bool {
        imgproc::Options o;
        o.target_w = tw;
        o.target_h = th;
        o.fit = imgproc::Fit::Contain;
        o.levels = 16;
        imgproc::Image img;
        set_phase(base, span);
        imgproc::Status st = imgproc::decode_buffer(pdf.data() + disp->offset, disp->length, o, img, &prog_);
        if (st != imgproc::Status::Ok) return false;
        cardstore::Blob meta = image_meta(img);
        return w.write_blob(id, img.data, (uint32_t)(img.stride * img.h), &meta);
    };

    // (2) Display cache, (3) Home preview cache — both from the same display JPEG.
    if (!decode_to_blob(cardstore::BLOB_DISPLAY, disp_w_, disp_h_, 10, 45)) {
        w.abort();
        state_.store(prog_.cancel.load() ? State::Cancelled : State::Failed);
        return;
    }
    if (!decode_to_blob(cardstore::BLOB_PREVIEW, 169, 300, 55, 30)) {
        w.abort();
        state_.store(prog_.cancel.load() ? State::Cancelled : State::Failed);
        return;
    }

    // (4) Name raster (LVGL canvas -> downscaled L8). Optional: an empty/failed
    // raster just leaves the name blob absent (Home shows no name image).
    set_phase(85, 0);
    if (!card.name.empty()) {
        nckpdf::GlyphSet gs;
        const nckpdf::GlyphSet *gsp = nullptr;
        if (glyph_a && glyph_a->length &&
            nckpdf::parse_name_glyphs(pdf.data() + glyph_a->offset, glyph_a->length, gs) == nckpdf::Status::Ok)
            gsp = &gs;
        lv_lock();
        NameRaster nr = render_name_l8(card.name.c_str(), gsp);
        lv_unlock();
        if (nr.valid()) {
            cardstore::Blob meta{};
            meta.w = nr.w;
            meta.h = nr.h;
            meta.stride = nr.stride;
            meta.levels = nr.levels;
            meta.format = cardstore::FMT_L8;
            w.write_blob(cardstore::BLOB_NAME, nr.data.data(), (uint32_t)nr.data.size(), &meta);
        }
    }
    if (cancelled()) { w.abort(); state_.store(State::Cancelled); return; }

    // (5) Commit the magic header last.
    set_phase(100, 0);
    if (!w.commit()) return fail();
    state_.store(State::Ok);
}

static void import_job_task(void *arg) {
    auto *keep = static_cast<std::shared_ptr<ImportJob> *>(arg);
    std::shared_ptr<ImportJob> job = *keep;
    delete keep;
    job->run();
    vTaskDelete(nullptr);
}

std::shared_ptr<ImportJob> ImportJob::start(const std::string &sd_path, uint16_t disp_w, uint16_t disp_h) {
    auto self = std::shared_ptr<ImportJob>(new ImportJob());
    self->path_ = sd_path;
    self->disp_w_ = disp_w;
    self->disp_h_ = disp_h;
    auto slash = sd_path.find_last_of('/');
    self->name_ = slash == std::string::npos ? sd_path : sd_path.substr(slash + 1);

#ifdef ESP_PLATFORM
    BaseType_t core = 1 - xPortGetCoreID();
    UBaseType_t prio = uxTaskPriorityGet(nullptr);
#else
    BaseType_t core = tskNO_AFFINITY;
    UBaseType_t prio = 1;
#endif
    auto *keep = new (std::nothrow) std::shared_ptr<ImportJob>(self);
    if (keep) {
        if (xTaskCreatePinnedToCore(import_job_task, "mcimport", 16384, keep, prio, nullptr, core) != pdPASS) {
            delete keep;
            self->run();  // fall back to synchronous
        }
    } else {
        self->run();
    }
    return self;
}

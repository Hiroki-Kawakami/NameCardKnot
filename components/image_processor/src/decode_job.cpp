/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "image_processor.hpp"

#include <utility>

#include "pipeline.hpp"

// The background decode runs on two imgf_async tasks (device: ESP-IDF FreeRTOS,
// simulator: pthreads). DecodeJob owns no tasks of its own — it sets up the
// pipeline synchronously, starts the imgf_async job, and reclaims it from the
// polling accessors once both tasks finish. Without FreeRTOS (host unit tests,
// IMGPROC_ASYNC=0) it decodes synchronously.
#ifndef IMGPROC_ASYNC
#define IMGPROC_ASYNC 1
#endif
#if IMGPROC_ASYNC && defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace imgproc {

DecodeJob::DecodeJob(const char *path, const Options &opts, int producer_core, int producer_prio,
                     int consumer_core, int consumer_prio, uint32_t offset, uint32_t length)
    : path_(path ? path : ""), opts_(opts),
      producer_core_(producer_core), producer_prio_(producer_prio),
      consumer_core_(consumer_core), consumer_prio_(consumer_prio),
      offset_(offset), length_(length) {}

DecodeJob::~DecodeJob() {
#if IMGPROC_ASYNC
    // Dropped mid-decode: cancel and join so the worker tasks never outlive the
    // job (they reference prog_ and the heap-owned decode state).
    if (async_) {
        prog_.cancel.store(true);
        Image discard;
        async_decode_join(async_, discard);
        async_ = nullptr;
    }
#endif
}

void DecodeJob::sync_() const {
#if IMGPROC_ASYNC
    if (async_ && async_decode_done(async_)) {
        Image img;
        Status st = async_decode_join(async_, img);
        async_ = nullptr;
        status_.store(st);
        if (st == Status::Ok) image_ = std::move(img);
        state_.store(st == Status::Ok         ? State::Ok
                     : st == Status::Cancelled ? State::Cancelled
                                               : State::Failed);
    }
#endif
}

int DecodeJob::progress_pct() const {
    sync_();
    int t = prog_.total.load();
    if (t <= 0) return 0;
    int d = prog_.done.load();
    return d >= t ? 100 : d * 100 / t;
}

DecodeJob::State DecodeJob::state() const {
    sync_();
    return state_.load();
}

Status DecodeJob::status() const {
    sync_();
    return status_.load();
}

Image DecodeJob::take_image() {
    sync_();
    return std::move(image_);
}

void DecodeJob::run() {
    Image img;
    Status st = decode_file(path_.c_str(), opts_, img, &prog_, offset_, length_);
    status_.store(st);
    if (st == Status::Ok) image_ = std::move(img);
    state_.store(st == Status::Ok         ? State::Ok
                 : st == Status::Cancelled ? State::Cancelled
                                           : State::Failed);
}

void DecodeJob::start() {
#if IMGPROC_ASYNC
    ParallelCfg par{producer_core_, producer_prio_, consumer_core_, consumer_prio_};
    Status setup = Status::Ok;
    async_ = async_decode_start(path_.c_str(), opts_, &prog_, par, offset_, length_, &setup);
    if (async_) return;  // running on two tasks; reclaimed by the polling accessors
    if (setup != Status::Ok) {
        status_.store(setup);
        state_.store(setup == Status::Cancelled ? State::Cancelled : State::Failed);
        return;
    }
    // setup == Ok with no handle → couldn't spawn the tasks → decode synchronously.
#endif
    run();
}

std::shared_ptr<DecodeJob> decode_file_async(const char *path, const Options &opts,
                                             int task_priority, int task_core,
                                             uint32_t offset, uint32_t length) {
#if IMGPROC_ASYNC
    // The decode (producer) keeps the caller's core/priority — the heavy, input-
    // dependent side; the color+dither (consumer) takes the other core. -1 ->
    // the core not running the caller / the caller's priority.
#ifdef ESP_PLATFORM
    int prod_core = task_core >= 0 ? task_core : 1 - (int)xPortGetCoreID();
    int prod_prio = task_priority >= 0 ? task_priority : (int)uxTaskPriorityGet(nullptr);
    int cons_core = 1 - prod_core;
    int cons_prio = prod_prio;
#else
    int prod_core = task_core;  // pthread imgf_async ignores cores
    int prod_prio = task_priority >= 0 ? task_priority : 0;
    int cons_core = -1;
    int cons_prio = prod_prio;
#endif
    auto job = std::make_shared<DecodeJob>(path, opts, prod_core, prod_prio, cons_core, cons_prio,
                                           offset, length);
    job->start();
#else
    (void)task_priority;
    (void)task_core;
    auto job = std::make_shared<DecodeJob>(path, opts, -1, -1, -1, -1, offset, length);
    job->run();
#endif
    return job;
}

}  // namespace imgproc

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "image_processor.hpp"

#include <new>
#include <utility>

// Async decode runs on its own FreeRTOS task (device: ESP-IDF, simulator:
// idf_compat). Without FreeRTOS (host unit tests) it falls back to synchronous.
#ifndef IMGPROC_ASYNC
#define IMGPROC_ASYNC 1
#endif
#if IMGPROC_ASYNC
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace imgproc {

DecodeJob::DecodeJob(const char *path, const Options &opts)
    : path_(path ? path : ""), opts_(opts) {}

int DecodeJob::progress_pct() const {
    int t = prog_.total.load();
    if (t <= 0) return 0;
    int d = prog_.done.load();
    return d >= t ? 100 : d * 100 / t;
}

Image DecodeJob::take_image() { return std::move(image_); }

void DecodeJob::run() {
    Image img;
    Status st = decode_file(path_.c_str(), opts_, img, &prog_);
    status_.store(st);
    if (st == Status::Ok) image_ = std::move(img);
    state_.store(st == Status::Ok         ? State::Ok
                 : st == Status::Cancelled ? State::Cancelled
                                           : State::Failed);
}

#if IMGPROC_ASYNC
// The task owns one shared_ptr ref (via `keep`), so the job outlives the worker
// regardless of when the app drops its handle.
static void decode_job_task(void *arg) {
    auto *keep = static_cast<std::shared_ptr<DecodeJob> *>(arg);
    std::shared_ptr<DecodeJob> job = *keep;
    delete keep;
    job->run();
    vTaskDelete(nullptr);
}
#endif

std::shared_ptr<DecodeJob> decode_file_async(const char *path, const Options &opts,
                                             int task_priority, int task_core) {
    auto job = std::make_shared<DecodeJob>(path, opts);
#if IMGPROC_ASYNC
    auto *keep = new (std::nothrow) std::shared_ptr<DecodeJob>(job);
    if (keep) {
        // -1 -> the caller's priority, the core not running it. Falls back to
        // synchronous if the task can't be created.
#ifdef ESP_PLATFORM
        BaseType_t core = task_core >= 0 ? (BaseType_t)task_core : 1 - xPortGetCoreID();
        UBaseType_t prio = task_priority >= 0 ? (UBaseType_t)task_priority
                                              : uxTaskPriorityGet(nullptr);
#else
        BaseType_t core = task_core >= 0 ? (BaseType_t)task_core : tskNO_AFFINITY;
        UBaseType_t prio = task_priority >= 0 ? (UBaseType_t)task_priority : 1;
#endif
        TaskHandle_t h = nullptr;
        if (xTaskCreatePinnedToCore(decode_job_task, "imgjob", 8192, keep, prio, &h, core) != pdPASS) {
            delete keep;
            job->run();
        }
    } else {
        job->run();
    }
#else
    job->run();
#endif
    return job;
}

}  // namespace imgproc

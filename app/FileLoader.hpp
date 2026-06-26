/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <string>

// The async-load interface the browser polls: progress, state, cancellation, and
// a label for the modal. It owns no UI and no completion logic — the browser
// drives the modal and the per-type transition. Implementations (e.g.
// NameCardData) stay LVGL-free.
class FileLoader {
public:
    enum class State { Loading, Ok, Failed, Cancelled };

    virtual ~FileLoader() = default;

    virtual State state() const = 0;
    virtual int progress_pct() const = 0;  // 0..100
    virtual void cancel() = 0;              // -> Cancelled once the worker stops
    virtual std::string label() const = 0;
};

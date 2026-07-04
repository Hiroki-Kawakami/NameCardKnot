/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <cstdint>
#include <string>

// One-shot "show a message at next boot" NVS record. `id` selects the UI
// BootMessageScreen builds; `param` is one free-form string argument.
namespace bootmsg {

enum class Id : uint32_t { None = 0, HotKnotShareFailed = 1, HotKnotReceiveFailed = 2 };

struct Info {
    Id id = Id::None;
    std::string param;
};

void save(Id id, const std::string &param);
Info take();
void clear();

}  // namespace bootmsg

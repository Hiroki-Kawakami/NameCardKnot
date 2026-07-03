/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <string>

// Which card NameCardScreen is showing, persisted in NVS so boot can reopen it
// directly (power-off resume). Saved on screen appear, cleared when the user
// leaves the screen.
namespace lastcard {

enum class Source : uint8_t { None = 0, MyCard = 1, SdFile = 2 };

struct Info {
    Source source = Source::None;
    std::string path;  // SdFile only
};

Info load();
void save_mycard();
void save_sd_file(const std::string &path);
void clear();

}  // namespace lastcard

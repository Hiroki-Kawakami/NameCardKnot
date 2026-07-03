/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <memory>
#include <string>

class NameCardData;

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

// The lastcard flash-partition cache (source PDF + decoded display image),
// written at sleep entry so boot can restore without the SD card or a decode.
// cache_path (NVS) names the source the partition contents are valid for: it is
// cleared before a rewrite and set once the cache is usable, so a write
// interrupted by reset can never pass off stale contents as the current card.
std::string cache_path();
void save_cache(const std::shared_ptr<NameCardData> &data);

// clean_resume = the glass shows a fully-rendered card (no modal), so boot can
// adopt it: the resume's first paint refreshes nothing and only the menu rect is
// driven, never re-flashing the card. NameCardScreen sets it while showing the
// card and clears it on leave / when a modal opens.
void set_clean_resume(bool clean_resume);
bool clean_resume();

}  // namespace lastcard

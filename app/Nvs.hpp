/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <cstdint>
#include <memory>
#include <string>

class NameCardData;

// All persisted app state lives in one NVS store (single namespace, keys ≤15
// chars and unique across the domains below). The typed get/set helpers are
// internal to Nvs.cpp; callers use these domain APIs.

// One-shot "show a message at next boot" record. `id` selects the UI
// BootMessageScreen builds; `param` is one free-form string argument.
namespace bootmsg {

enum class Id : uint32_t { None = 0, ShareFailed = 1, ReceiveFailed = 2, TransferFailed = 3, TransferComplete = 4 };

struct Info {
    Id id = Id::None;
    std::string param;
};

void save(Id id, const std::string &param);
Info take();
void clear();

}  // namespace bootmsg

// Which card NameCardScreen is showing, persisted so boot can reopen it directly
// (power-off resume). Saved on screen appear, cleared when the user leaves.
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

// One-shot pending-open record for a just-received card (path under
// RECEIVED_CARDS_DIR), consumed at the next boot.
void save_received(const std::string &path);
std::string take_received();

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

// Persisted user preferences. Add a setting as an accessor pair here, defined
// over the internal typed helpers in Nvs.cpp.
namespace settings {

bool share_receive_return();          // ShareScreen "Also receive their card in return"
void set_share_receive_return(bool value);
bool receive_send_return();           // ReceiveScreen "Also send my card in return"
void set_receive_send_return(bool value);

std::string language();               // "en"/"ja", "" = unset (first-boot picker)
void set_language(const std::string &lang);

}  // namespace settings

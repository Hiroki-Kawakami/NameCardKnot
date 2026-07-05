/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

// UI string table: one member per distinct English source string (shared
// across screens where identical), plus printf-style format members. S()
// reads the active table; strings::set() switches it (En by default).
struct Strings {
    // Shared across screens
    const char *app_name;
    const char *close;
    const char *cancel;
    const char *ok;
    const char *back;
    const char *home;
    const char *settings;
    const char *share;
    const char *receive;
    const char *gallery;
    const char *url;
    const char *message;
    const char *error;
    const char *no_items;
    const char *sd_card_not_found;
    const char *loading;
    const char *cancelling;
    const char *cannot_open_fmt;       // "Cannot open %s"
    const char *items_page_fmt;        // "%d Items (%d/%d)"

    // Home
    const char *open_from_sd;
    const char *my_card;
    const char *no_my_card;
    const char *import_from_sd;

    // Settings
    const char *repository;
    const char *developer;
    const char *date_time;
    const char *languages;
    const char *acknowledgements;

    // NameCard
    const char *no_image;

    // SharedCard
    const char *cannot_load_image;
    const char *image_1;
    const char *image_2;

    // Share
    const char *sd_required_to_receive;
    const char *also_receive_return;

    // Receive
    const char *sd_required_to_receive_cards;
    const char *invalid_descriptor;
    const char *also_send_return;

    // Transfer
    const char *handshaking;
    const char *exchanging_card;
    const char *sending_card;
    const char *receiving_card;
    const char *finalizing;
    const char *received_new_card;
    const char *card_sent;
    const char *no_cards_exchanged;
    const char *could_not_connect;
    const char *failed_save_received;
    const char *connection_lost;
    const char *error_detail_fmt;      // "%s (%s)"
    const char *transfer_progress_fmt; // "%u.%u / %u.%u KB"

    // BootMessage
    const char *notice;
    const char *share_failed;
    const char *receive_failed;
    const char *transfer_failed;
    const char *transfer_complete;
    const char *press_reset_hint;
    const char *open_card;

    // HotKnot
    const char *devices_separated;
    const char *card_exchange_failed;
    const char *hold_screen_hint;
    const char *hotknot_approach;
    const char *keep_held_hint;
    const char *connection_failed_retry;

    // Sleep
    const char *hold_button_wake;
    const char *press_button_wake;

    // LanguageSelect (title only — button labels are language names, invariant)
    const char *language_title;
};

const Strings &S();

namespace strings {

enum class Lang { En, Ja };
void set(Lang lang);

}  // namespace strings

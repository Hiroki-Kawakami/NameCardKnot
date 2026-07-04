/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp.h"
#include <memory>

class NameCardScreen;

// dokan app identifier (descriptor gate + KDF salt).
#define DOKAN_APP_ID "NCKN"

// SD-card directory collecting cards received from peers (.snc.pdf).
#define RECEIVED_CARDS_DIR "/sdcard/ReceivedCards"

// Fixed descriptor for the dokan transfer test (ShareScreen <-> ReceiveScreen
// over WiFi). Throwaway test value — the real flow exchanges a fresh descriptor
// over HotKnot. WiFi transport, app "NCKN", channel 6, port 3939, fixed seed.
#define DOKAN_TEST_DESCRIPTOR "DK1NCKNWAAYQERITFBUWFxgZGhscHR4fD2P1"

void epd_set_default_refresh_mode(bsp_epd_mode_t mode);
void epd_set_next_refresh_mode(bsp_epd_mode_t mode);

// Sets the SoC system clock from the RTC (no-op if the RTC has no valid time).
void rtc_sync_system_time();

bool mount_sd_card();
void unmount_sd_card();

// Rebuilds the NameCardScreen for the card recorded in lastcard NVS, or nullptr
// if nothing is resumable. Leaves clean_resume untouched (only the boot path
// may adopt it).
std::shared_ptr<NameCardScreen> make_resumed_card_screen();

void app_entry();

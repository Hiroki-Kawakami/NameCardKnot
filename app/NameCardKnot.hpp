/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp.h"

// dokan app identifier (descriptor gate + KDF salt).
#define DOKAN_APP_ID "NCKN"

// Fixed descriptor for the dokan transfer test (ShareScreen <-> ReceiveScreen
// over WiFi). Throwaway test value — the real flow exchanges a fresh descriptor
// over HotKnot. WiFi transport, app "NCKN", channel 6, port 3939, fixed seed.
#define DOKAN_TEST_DESCRIPTOR "DK1NCKNWAAYQERITFBUWFxgZGhscHR4fD2P1"

void epd_set_default_refresh_mode(bsp_epd_mode_t mode);
void epd_set_next_refresh_mode(bsp_epd_mode_t mode);

bool mount_sd_card();
void unmount_sd_card();

void app_entry();

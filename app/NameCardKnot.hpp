/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp.h"

void epd_set_default_refresh_mode(bsp_epd_mode_t mode);
void epd_set_next_refresh_mode(bsp_epd_mode_t mode);

void app_entry();

#pragma once
#include "bsp.h"

void epd_set_default_refresh_mode(bsp_epd_mode_t mode);
void epd_set_next_refresh_mode(bsp_epd_mode_t mode);

void app_entry();

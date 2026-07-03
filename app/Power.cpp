/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "Power.hpp"
#include "NameCardKnot.hpp"
#include "bsp.h"
#include "lvgl.hpp"
#include "screen_manager.hpp"
#include "NameCardScreen.hpp"
#include "SleepScreen.hpp"
#include <cstdlib>

namespace power {

static constexpr uint32_t kDefaultTimeoutMs = 5 * 60 * 1000;

static Screen         *s_owner;
static uint32_t        s_owner_timeout;
static NameCardScreen *s_card;
static bool            s_sleeping;
#ifndef ESP_PLATFORM
static uint32_t        s_override;   // SIMULATOR_SLEEP_TIMEOUT_MS
#endif

void set_timeout(Screen *owner, uint32_t ms) {
    s_owner = owner;
    s_owner_timeout = ms;
}

void kick() {
    lv_display_trigger_activity(NULL);
}

bool sleeping() {
    return s_sleeping;
}

void set_card_screen(NameCardScreen *screen) {
    s_card = screen;
}

static void go_to_sleep() {
    s_sleeping = true;
    Screen *cur = screen_manager.current_screen();
    if (s_card && cur == s_card) {
        // Menu open: restore the bare card (ghost-free for the long static
        // display). Else the glass already shows it — draw nothing.
        if (s_card->closeModal()) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            lv_refr_now(NULL);
        }
    } else if (s_card) {
        // A screen pushed from the card: pop back to it. Transitions don't
        // render by themselves, so only the final card state is painted — with
        // QUALITY (the card's sleeping onAppear), so unchanged pixels diff-skip.
        while (screen_manager.current_screen() &&
               screen_manager.current_screen() != s_card) {
            screen_manager.pop();
        }
        lv_refr_now(NULL);
    } else {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        screen_manager.load(std::make_shared<SleepScreen>());
        lv_refr_now(NULL);
    }

    unmount_sd_card();
    bsp_hotknot_end();      // teardown belongs to the session's screen; this is a backstop
    bsp_rtc_timer_stop();   // a stale countdown would re-power the board
    bsp_display_wait_idle();
    bsp_power_off();

    // Still here: USB keeps VSYS up. Stay on and retry after another full
    // idle period.
    s_sleeping = false;
    kick();
}

static void watchdog_cb(lv_timer_t *) {
    Screen *cur = screen_manager.current_screen();
    if (!cur) return;
    uint32_t timeout = (s_owner == cur) ? s_owner_timeout : kDefaultTimeoutMs;
#ifndef ESP_PLATFORM
    if (s_override && timeout) timeout = s_override;
#endif
    if (!timeout || lv_display_get_inactive_time(NULL) < timeout) return;
    go_to_sleep();
}

void start() {
#ifndef ESP_PLATFORM
    if (const char *e = std::getenv("SIMULATOR_SLEEP_TIMEOUT_MS")) {
        s_override = (uint32_t)std::strtoul(e, nullptr, 10);
    }
#endif
    lv_timer_create(watchdog_cb, 1000, nullptr);
}

}  // namespace power

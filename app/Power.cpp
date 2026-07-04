/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "Power.hpp"
#include "NameCardKnot.hpp"
#include "Nvs.hpp"
#include "bsp.h"
#include "lvgl.hpp"
#include "screen_manager.hpp"
#include "NameCardScreen.hpp"
#include "SleepScreen.hpp"
#include "esp_log.h"
#include <cstdlib>

static const char *TAG = "power";

namespace power {

static constexpr uint32_t kDefaultTimeoutMs = 5 * 60 * 1000;

static Screen         *s_owner;
static uint32_t        s_owner_timeout;
static NameCardScreen *s_card;
static bool            s_sleeping;
static bool            s_off_pending;   // sleep drawn + torn down; power-off failed (USB), retry bare
static uint32_t        s_off_tick;      // lv_tick of the last power-off attempt (retry spacing)
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
    if (!s_off_pending) {
        Screen *cur = screen_manager.current_screen();
        if (s_card && cur == s_card) {
            // Menu/modal open: restore the bare card (ghost-free for the long
            // static display). Else the glass already shows it — draw nothing.
            if (s_card->closeOverlays()) {
                s_card->clearDisplay();
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

        bsp_display_wait_idle();   // park the EPD before flash writes stall the cache
        lastcard::save_cache(s_card ? s_card->data() : nullptr);
        unmount_sd_card();
        bsp_hotknot_end();      // teardown belongs to the session's screen; this is a backstop
        bsp_rtc_timer_stop();   // a stale countdown would re-power the board
    }

    ESP_LOGI(TAG, "power off (card=%d)", s_card != nullptr);
    s_off_tick = lv_tick_get();
    bsp_power_off();

    // Still here: USB keeps VSYS up. The glass already shows the sleep state, so
    // later retries skip the render/teardown above and only re-attempt power-off
    // (spaced by the watchdog); a user touch clears the pending state.
    s_off_pending = true;
    s_sleeping = false;
}

static void watchdog_cb(lv_timer_t *) {
    Screen *cur = screen_manager.current_screen();
    if (!cur) return;
    uint32_t timeout = (s_owner == cur) ? s_owner_timeout : kDefaultTimeoutMs;
#ifndef ESP_PLATFORM
    if (s_override && timeout) timeout = s_override;
#endif
    uint32_t inactive = lv_display_get_inactive_time(NULL);
    if (s_off_pending) {
        // Power-off failed on USB: a fresh touch (or a never-sleep screen) means
        // the device woke — resume normal timing. Otherwise retry the bare
        // power-off once another idle period elapses (no EPD refresh).
        if (!timeout || inactive < timeout) s_off_pending = false;
        else if (lv_tick_elaps(s_off_tick) >= timeout) go_to_sleep();
        return;
    }
    if (!timeout || inactive < timeout) return;
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

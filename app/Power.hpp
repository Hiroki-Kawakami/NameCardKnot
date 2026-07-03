/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <cstdint>

class Screen;
class NameCardScreen;

// Idle-timeout power-off (app policy, like the EPD refresh modes). A 1s
// watchdog powers the board off after inactivity; screens declare a
// non-default timeout in onAppear, which reverts to the 5min default as soon
// as that screen stops being current.
namespace power {

void start();
void set_timeout(Screen *owner, uint32_t ms);  // 0 = never sleep
void kick();                                   // non-touch activity resets the countdown
bool sleeping();                               // true while the sleep sequence renders

// Registered by NameCardScreen while it is on the stack: sleeping then shows
// the card (popping back to it if needed) instead of the SleepScreen.
void set_card_screen(NameCardScreen *screen);

// The active screen is showing just the card — a refresh issued now leaves the
// glass seedable (the flush hook mirrors this into lastcard::set_clean).
bool bare_card_displayed();

}  // namespace power

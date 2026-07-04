/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <cstdint>
#include <string>

// Persisted user preferences (NVS namespace "settings").
//
// Add a setting by declaring an accessor pair here and defining it over the
// typed helpers in Settings.cpp — keeping every persisted key in one place.
namespace settings {

// Generic typed store; keys are ≤15 chars (NVS limit).
bool get_bool(const char *key, bool def);
void set_bool(const char *key, bool value);
uint32_t get_u32(const char *key, uint32_t def);
void set_u32(const char *key, uint32_t value);
std::string get_str(const char *key, const std::string &def);
void set_str(const char *key, const std::string &value);

// Named preferences.
bool share_receive_return();          // ShareScreen "Also receive their card in return"
void set_share_receive_return(bool value);
bool receive_send_return();           // ReceiveScreen "Also send my card in return"
void set_receive_send_return(bool value);

}  // namespace settings

// What the book transfer (soft access point) and the updater (station on
// the home network) share: one-time network setup, and the home Wi-Fi
// network saved in flash (NVS).
#pragma once

#include <cstddef>

// NVS, the network interface layer and the default event loop, once.
bool wifi_base_init();

// The home network set from the phone page. Returns false when none is set.
bool home_wifi_load(char* ssid, size_t ssid_size, char* password, size_t password_size);
bool home_wifi_save(const char* ssid, const char* password);

// What the book transfer (soft access point), the updater and the store
// (stations on the home network) share: one-time network setup, the home
// Wi-Fi network saved in flash (NVS), and joining it.
#pragma once

#include <cstddef>

#include "esp_event.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "vellum/hal.h"

// NVS, the network interface layer and the default event loop, once.
bool wifi_base_init();

// The home network set from the phone page. Returns false when none is set.
bool home_wifi_load(char* ssid, size_t ssid_size, char* password, size_t password_size);
bool home_wifi_save(const char* ssid, const char* password);

// Station mode on the home network, for one update check or install, or
// while the store is open. Keeps the chip out of light sleep meanwhile.
class HomeWifi {
 public:
  ~HomeWifi() { leave(); }
  // Returns UpToDate once joined, else NoNetwork (none set) or NoWifi.
  // While it waits, cancelled(ctx) is asked every kPollMs when given; it
  // gives up (NoWifi) as soon as that returns true.
  using CancelFn = bool (*)(void* ctx);
  vellum::UpdateResult join(CancelFn cancelled = nullptr, void* ctx = nullptr);
  // Disconnects and turns the radio off.
  void leave();
  bool joined() const { return joined_; }

 private:
  static void on_event(void* arg, esp_event_base_t base, int32_t, void*);

  EventGroupHandle_t events_ = nullptr;
  static inline esp_pm_lock_handle_t awake_ = nullptr;
  bool held_ = false;
  bool on_ = false;
  bool joined_ = false;
};

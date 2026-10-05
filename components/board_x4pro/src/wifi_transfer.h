// Receiving books over Wi-Fi: the reader opens its own network (a soft
// access point, so no router or password typing is needed) or, when the app
// asks, joins the home Wi-Fi; it serves the upload page and saves books to
// the SD card's books folder. The page's Settings section changes files on
// the card (settings, sync account, catalogues) for the app to take up.
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "vellum/hal.h"

class WifiTransfer : public vellum::Transfer {
 public:
  using NotifyFn = void (*)();

  // `card_root` is where the SD card is mounted; `notify` is called (from
  // other tasks) whenever the status changes. The upload page sets `clock`
  // (may be nullptr) from the phone's time.
  WifiTransfer(const char* card_root, NotifyFn notify, vellum::Clock* clock);

  bool start(vellum::TransferInfo& info) override;
  void stop() override;
  void status(vellum::TransferStatus& out) override;

 private:
  struct Impl;
  Impl* impl_;
};

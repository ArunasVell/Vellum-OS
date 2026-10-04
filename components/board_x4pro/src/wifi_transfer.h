// Receiving books over Wi-Fi: the reader opens its own network (a soft
// access point, so no router or password typing is needed), serves the
// upload page and saves books to the SD card's books folder.
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "vellum/hal.h"

class WifiTransfer : public vellum::Transfer {
 public:
  using NotifyFn = void (*)();

  // `card_root` is where the SD card is mounted; `notify` is called (from
  // other tasks) whenever the status changes.
  WifiTransfer(const char* card_root, NotifyFn notify);

  bool start(vellum::TransferInfo& info) override;
  void stop() override;
  void status(vellum::TransferStatus& out) override;

 private:
  struct Impl;
  Impl* impl_;
};

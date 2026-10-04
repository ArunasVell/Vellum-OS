// BM8563 real-time clock (register-compatible with the NXP PCF8563).
//
// Keeps time on the X4 Pro while the reader sleeps. Times are stored as
// local time; the Wi-Fi upload page sets it from the phone's clock.
#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "vellum/hal.h"

class Bm8563 : public vellum::Clock {
 public:
  static constexpr uint8_t kAddress = 0x51;

  // Adds the clock to `bus`. Returns false if the device cannot be added.
  bool attach(i2c_master_bus_handle_t bus, uint32_t scl_hz);

  bool now(vellum::DateTime& out) override;
  bool set(const vellum::DateTime& t) override;

 private:
  i2c_master_dev_handle_t dev_ = nullptr;
};

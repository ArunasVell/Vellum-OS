// CellWise CW2017 fuel gauge, ported from FreeInk's BatteryMonitor (MIT).
//
// The gauge reports 0 % until it holds the battery profile for the X4 Pro's
// cell, so start() checks the resident profile and uploads it when missing.
#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

class Cw2017 {
 public:
  static constexpr uint8_t kAddress = 0x63;

  // Adds the gauge to `bus`. Returns false if the device cannot be added.
  bool attach(i2c_master_bus_handle_t bus, uint32_t scl_hz);

  // Makes sure the gauge runs with the right profile. Can take a few seconds
  // after a profile upload; returns false on any I2C error so callers retry.
  bool start();

  // State of charge 0..100, or -1 when unavailable. Restarts the gauge if it
  // is found asleep or misconfigured.
  int percent();
  // Cell voltage in millivolts, or -1 when unavailable.
  int millivolts();

 private:
  bool read(uint8_t reg, uint8_t& out);
  bool write(uint8_t reg, uint8_t value);
  bool reset();
  bool wait_ready();
  bool profile_matches(bool& matches);

  i2c_master_dev_handle_t dev_ = nullptr;
  bool started_ = false;
  int64_t last_start_us_ = 0;
};

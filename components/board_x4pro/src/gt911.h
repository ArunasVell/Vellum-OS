// Goodix GT911 capacitive touch controller.
//
// Wiring and bring-up follow the FreeInk SDK's X4 Pro notes (MIT): powered
// by GPIO2 low, reset on GPIO4, interrupt on GPIO10, address 0x5D, and the
// controller loads its own configuration after reset (none is uploaded).
// The Home pad is a capacitive key the GT911 reports in its status byte.
#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

class Gt911 {
 public:
  static constexpr uint8_t kAddress = 0x5D;

  struct Report {
    bool touching = false;
    int x = 0;  // first finger, in the controller's own axes
    int y = 0;
    bool home = false;  // Home pad pressed
  };

  // Resets the controller (selecting address 0x5D with the interrupt pin),
  // adds it to `bus` and checks that it answers. Leaves the interrupt pin
  // as an input. Returns false when no GT911 answers.
  bool attach(i2c_master_bus_handle_t bus, uint32_t scl_hz, int rst_gpio, int int_gpio);

  // Reads the newest report. Returns false when there is none (the
  // controller has nothing new, or the bus failed).
  bool read(Report& out);

  // The touch area the controller is configured for.
  int max_x() const { return max_x_; }
  int max_y() const { return max_y_; }

 private:
  bool read_reg(uint16_t reg, uint8_t* buf, size_t len);
  bool write_reg(uint16_t reg, uint8_t value);

  i2c_master_dev_handle_t dev_ = nullptr;
  int max_x_ = 0;
  int max_y_ = 0;
};

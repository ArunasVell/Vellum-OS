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
  static constexpr uint8_t kAltAddress = 0x14;

  struct Report {
    bool touching = false;
    int x = 0;  // first finger, in the controller's own axes
    int y = 0;
    bool home = false;  // Home pad pressed
  };

  // Resets the controller (selecting address 0x5D with the interrupt pin,
  // or 0x14 if it does not answer there), adds it to `bus` and checks that
  // it answers, a few times over. Leaves the interrupt pin as an input.
  // Returns false when no GT911 answers.
  bool attach(i2c_master_bus_handle_t bus, uint32_t scl_hz, int rst_gpio, int int_gpio);

  // Reads the newest report. Returns false when there is none (the
  // controller has nothing new, or the bus failed).
  bool read(Report& out);

  // Sends the controller to sleep, its lowest-power state short of losing
  // power: before deep sleep, in case its power switch leaks. Only a reset
  // (attach) wakes it. Its interrupt pin becomes an output, held low.
  void sleep();

  // The touch area the controller is configured for.
  int max_x() const { return max_x_; }
  int max_y() const { return max_y_; }

  // For the device check: the address that answered (0: none), its
  // configuration version, reads that brought a report or failed, and the
  // last status byte read.
  uint8_t address() const { return address_; }
  int config_version() const { return config_version_; }
  uint32_t reports() const { return reports_; }
  uint32_t failures() const { return failures_; }
  uint8_t last_status() const { return last_status_; }
  int resets() const { return resets_; }  // resets it took to start

 private:
  bool reset_and_probe(i2c_master_bus_handle_t bus, uint32_t scl_hz, int rst_gpio, int int_gpio, uint8_t address,
                       int slow, bool need_config);
  bool read_reg(uint16_t reg, uint8_t* buf, size_t len);
  bool write_reg(uint16_t reg, uint8_t value);

  i2c_master_dev_handle_t dev_ = nullptr;
  int int_gpio_ = -1;
  int max_x_ = 0;
  int max_y_ = 0;
  uint8_t address_ = 0;
  int config_version_ = 0;
  uint32_t reports_ = 0;
  uint32_t failures_ = 0;
  uint8_t last_status_ = 0;
  int resets_ = 0;
};

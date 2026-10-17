// SPI link to an e-paper controller: command/data framing, reset, BUSY waits.
//
// Ported from the FreeInk SDK EpdBus (MIT, see NOTICE) to ESP-IDF's spi_master.
// CS is driven by hand so a whole plane can be streamed in one CS-low burst.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "driver/spi_master.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace epd {

struct Pins {
  int sclk;
  int mosi;
  int cs;
  int dc;
  int rst;
  int busy;
};

class Bus {
 public:
  // Requires the GPIO ISR service to be installed. Returns false on failure.
  bool init(const Pins& pins, uint32_t spi_hz);

  // Reset pulse: high 10 ms, low 10 ms, high 10 ms, then `settle_ms`.
  void reset(uint32_t settle_ms = 0);

  void cmd(uint8_t c);
  void data(uint8_t d);
  void cmd_data(uint8_t c, std::initializer_list<uint8_t> d);

  // Streams a RAM plane after `c` with CS held low throughout.
  void stream_begin(uint8_t c);
  void stream_bytes(const uint8_t* src, size_t n, bool invert = false);
  void stream_fill(uint8_t value, size_t n);
  void stream_end();

  // Level of the BUSY pin (1 = high).
  int busy_level() const;
  // Sleeps until BUSY reads `level` or `timeout_ms` passes. Returns false on
  // timeout, with a warning in the log unless `quiet`.
  bool wait_for(int level, uint32_t timeout_ms, bool quiet = false);

  // A power management lock to let go of while waiting for BUSY (nullptr:
  // none): a refresh is the panel's work, so the CPU can slow down meanwhile.
  void set_relax(esp_pm_lock_handle_t lock) { relax_ = lock; }

  static void delay_ms(uint32_t ms);

 private:
  void write(const uint8_t* d, size_t n);
  void flush();
  static void busy_isr(void* arg);

  static constexpr size_t kChunk = 4000;  // multiple of 4 and of one 800 px row

  Pins pins_{};
  spi_device_handle_t dev_ = nullptr;
  SemaphoreHandle_t busy_edge_ = nullptr;
  uint8_t* chunk_ = nullptr;  // DMA-capable staging buffer
  size_t fill_ = 0;
  esp_pm_lock_handle_t relax_ = nullptr;
};

}  // namespace epd

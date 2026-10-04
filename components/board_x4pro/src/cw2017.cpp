#include "cw2017.h"

#include <initializer_list>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr uint8_t kRegVersion = 0x00;   // 0xA0 while starting; 0x0D or 0x0F when running
constexpr uint8_t kRegVcell = 0x02;     // 14-bit, big-endian over 0x02/0x03
constexpr uint8_t kRegSoc = 0x04;       // whole percent
constexpr uint8_t kRegMode = 0x08;      // reset and sleep control
constexpr uint8_t kRegSocAlert = 0x0B;  // bit 7: profile loaded
constexpr uint8_t kRegBatinfo = 0x10;   // 80-byte profile, 0x10..0x5F

constexpr uint8_t kModeNormal = 0x00;
constexpr uint8_t kModeRestart = 0x30;
constexpr uint8_t kModeDefault = 0xF0;
constexpr uint8_t kUpdateFlag = 0x80;
constexpr int64_t kRetryUs = 1000 * 1000;
constexpr int kTimeoutMs = 50;

// Battery profile the stock X4 Pro firmware uploads, recovered by FreeInk.
constexpr uint8_t kBatinfo[80] = {0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xbf, 0xb5, 0xb4, 0xa4, 0x9c,
                                  0xeb, 0xe2, 0xdf, 0xe5, 0xca, 0xa0, 0x8a, 0x62, 0x53, 0x48, 0x40, 0x3a, 0x32, 0xb1,
                                  0xae, 0xda, 0xb5, 0xff, 0xff, 0xff, 0xe8, 0xdb, 0xd9, 0xd6, 0xd4, 0xd2, 0xd0, 0xcb,
                                  0xc3, 0xbc, 0x9e, 0x87, 0x7b, 0x71, 0x72, 0x7c, 0x8c, 0xa3, 0xb7, 0xc8, 0xa5, 0x4f,
                                  0x00, 0x00, 0xab, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23};

bool running(uint8_t version) { return (version & 0xFD) == 0x0D; }
void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

}  // namespace

bool Cw2017::attach(i2c_master_bus_handle_t bus, uint32_t scl_hz) {
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = kAddress;
  cfg.scl_speed_hz = scl_hz;
  return i2c_master_bus_add_device(bus, &cfg, &dev_) == ESP_OK;
}

bool Cw2017::read(uint8_t reg, uint8_t& out) {
  return dev_ != nullptr && i2c_master_transmit_receive(dev_, &reg, 1, &out, 1, kTimeoutMs) == ESP_OK;
}

bool Cw2017::write(uint8_t reg, uint8_t value) {
  const uint8_t buf[2] = {reg, value};
  return dev_ != nullptr && i2c_master_transmit(dev_, buf, sizeof(buf), kTimeoutMs) == ESP_OK;
}

// Soft reset as the stock firmware does it: MODE F0, 30, 00, 20 ms apart.
bool Cw2017::reset() {
  for (uint8_t m : {kModeDefault, kModeRestart, kModeNormal}) {
    if (!write(kRegMode, m)) return false;
    sleep_ms(20);
  }
  return true;
}

// About 1 s for the engine to start, then up to 3 s for a valid charge value.
bool Cw2017::wait_ready() {
  bool up = false;
  for (int i = 0; i < 50 && !up; ++i) {
    uint8_t v = 0;
    up = read(kRegVersion, v) && running(v);
    if (!up) sleep_ms(20);
  }
  if (!up) return false;
  for (int i = 0; i < 30; ++i) {
    uint8_t soc = 0;
    if (read(kRegSoc, soc) && soc <= 100) return true;
    sleep_ms(100);
  }
  return false;
}

// An I2C error is reported separately from a mismatch, so a bus glitch never
// triggers a partial profile rewrite.
bool Cw2017::profile_matches(bool& matches) {
  uint8_t flags = 0;
  if (!read(kRegSocAlert, flags)) return false;
  matches = false;
  if ((flags & kUpdateFlag) == 0) return true;
  for (uint8_t i = 0; i < sizeof(kBatinfo); ++i) {
    uint8_t b = 0;
    if (!read(static_cast<uint8_t>(kRegBatinfo + i), b)) return false;
    if (b != kBatinfo[i]) return true;
  }
  matches = true;
  return true;
}

bool Cw2017::start() {
  uint8_t mode = 0;
  uint8_t version = 0;
  if (!read(kRegMode, mode) || !read(kRegVersion, version)) return false;
  bool matches = false;
  if (!profile_matches(matches)) return false;

  bool restart = mode != kModeNormal || !running(version);
  if (!matches) {
    for (uint8_t i = 0; i < sizeof(kBatinfo); ++i) {
      if (!write(static_cast<uint8_t>(kRegBatinfo + i), kBatinfo[i])) return false;
    }
    if (!write(kRegSocAlert, kUpdateFlag)) return false;
    sleep_ms(20);
    restart = true;
  }
  if (!restart) {
    uint8_t soc = 0;
    if (!read(kRegSoc, soc)) return false;
    if (soc <= 100) return true;
  }
  return reset() && wait_ready();
}

int Cw2017::percent() {
  if (!started_) {
    // Retry at most once a second: the bus is shared with touch and the RTC.
    const int64_t now = esp_timer_get_time();
    if (last_start_us_ != 0 && now - last_start_us_ < kRetryUs) return -1;
    last_start_us_ = now;
    started_ = start();
    if (!started_) return -1;
  }
  uint8_t mode = 0;
  uint8_t version = 0;
  uint8_t soc = 0;
  if (!read(kRegMode, mode) || mode != kModeNormal || !read(kRegVersion, version) || !running(version) ||
      !read(kRegSoc, soc) || soc > 100) {
    started_ = false;  // next call runs the bounded recovery
    return -1;
  }
  return soc;
}

int Cw2017::millivolts() {
  uint8_t hi = 0;
  uint8_t lo = 0;
  if (!read(kRegVcell, hi) || !read(static_cast<uint8_t>(kRegVcell + 1), lo)) return -1;
  const uint32_t raw = (static_cast<uint32_t>(hi & 0x3F) << 8) | lo;
  return static_cast<int>((raw * 5 + 8) >> 4);  // stock formula, about 0.3125 mV per step
}

#include "bm8563.h"

namespace {

constexpr uint8_t kRegControl2 = 0x01;
constexpr uint8_t kRegSeconds = 0x02;  // bit 7: voltage low, time not reliable
constexpr uint8_t kVoltageLow = 0x80;
constexpr uint8_t kCentury = 0x80;  // month register: set for 1900s, clear for 2000s
constexpr int kTimeoutMs = 50;

uint8_t from_bcd(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t to_bcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

}  // namespace

bool Bm8563::attach(i2c_master_bus_handle_t bus, uint32_t scl_hz) {
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = kAddress;
  cfg.scl_speed_hz = scl_hz;
  return i2c_master_bus_add_device(bus, &cfg, &dev_) == ESP_OK;
}

bool Bm8563::now(vellum::DateTime& out) {
  if (dev_ == nullptr) return false;
  // Seconds, minutes, hours, day, weekday, month, year in one burst.
  uint8_t r[7];
  const uint8_t reg = kRegSeconds;
  if (i2c_master_transmit_receive(dev_, &reg, 1, r, sizeof(r), kTimeoutMs) != ESP_OK) return false;
  if (r[0] & kVoltageLow) return false;
  out.second = from_bcd(r[0] & 0x7F);
  out.minute = from_bcd(r[1] & 0x7F);
  out.hour = from_bcd(r[2] & 0x3F);
  out.day = from_bcd(r[3] & 0x3F);
  out.month = from_bcd(r[5] & 0x1F);
  out.year = static_cast<uint16_t>(((r[5] & kCentury) ? 1900 : 2000) + from_bcd(r[6]));
  return out.month >= 1 && out.month <= 12 && out.hour < 24 && out.minute < 60;
}

bool Bm8563::set(const vellum::DateTime& t) {
  if (dev_ == nullptr || t.year < 2000 || t.year > 2099) return false;
  // Writing the seconds register clears the voltage-low flag. Weekday is unused.
  const uint8_t buf[9] = {kRegControl2,  0x00, to_bcd(t.second), to_bcd(t.minute),    to_bcd(t.hour),
                          to_bcd(t.day), 0x00, to_bcd(t.month),  to_bcd(t.year % 100)};
  return i2c_master_transmit(dev_, buf, sizeof(buf), kTimeoutMs) == ESP_OK;
}

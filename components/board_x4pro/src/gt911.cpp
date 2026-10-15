#include "gt911.h"

#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char* kTag = "gt911";
constexpr int kTimeoutMs = 20;

constexpr uint16_t kRegCommand = 0x8040;    // 0x05: sleep
constexpr uint16_t kRegConfig = 0x8047;     // config version, then X max, Y max (little endian)
constexpr uint16_t kRegProductId = 0x8140;  // "911" in ASCII
constexpr uint16_t kRegStatus = 0x814E;     // bit 7: report ready, bit 4: key, bits 0-3: fingers
constexpr uint16_t kRegPoint1 = 0x8150;     // X lo, X hi, Y lo, Y hi, size lo, size hi, ...
constexpr uint8_t kReady = 0x80;
constexpr uint8_t kKey = 0x10;

void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1); }

}  // namespace

bool Gt911::read_reg(uint16_t reg, uint8_t* buf, size_t len) {
  const uint8_t addr[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
  return i2c_master_transmit_receive(dev_, addr, sizeof(addr), buf, len, kTimeoutMs) == ESP_OK;
}

bool Gt911::write_reg(uint16_t reg, uint8_t value) {
  const uint8_t buf[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), value};
  return i2c_master_transmit(dev_, buf, sizeof(buf), kTimeoutMs) == ESP_OK;
}

bool Gt911::reset_and_probe(i2c_master_bus_handle_t bus, uint32_t scl_hz, int rst_gpio, int int_gpio, uint8_t address) {
  const auto rst = static_cast<gpio_num_t>(rst_gpio);
  const auto irq = static_cast<gpio_num_t>(int_gpio);

  // Reset: the interrupt pin's level as RST rises selects the address (low:
  // 0x5D, high: 0x14). Too short a reset and the controller never loads its
  // configuration, so keep the FreeInk SDK's timings (10 / 10 / 50 / 50 ms).
  gpio_config_t out = {};
  out.pin_bit_mask = (1ULL << rst_gpio) | (1ULL << int_gpio);
  out.mode = GPIO_MODE_OUTPUT;
  gpio_config(&out);
  gpio_set_level(rst, 0);
  gpio_set_level(irq, address == kAltAddress ? 1 : 0);
  delay_ms(10);
  gpio_set_level(rst, 1);
  delay_ms(10);
  gpio_config_t in = {};
  in.pin_bit_mask = 1ULL << int_gpio;
  in.mode = GPIO_MODE_INPUT;
  in.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&in);
  delay_ms(100);

  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = address;
  cfg.scl_speed_hz = scl_hz;
  if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) return false;
  char id[5] = {};
  if (!read_reg(kRegProductId, reinterpret_cast<uint8_t*>(id), 4)) {
    ESP_LOGW(kTag, "no answer at 0x%02X", address);
    i2c_master_bus_rm_device(dev_);
    dev_ = nullptr;
    return false;
  }
  uint8_t c[5] = {};
  if (read_reg(kRegConfig, c, sizeof(c))) {
    max_x_ = c[1] | (c[2] << 8);
    max_y_ = c[3] | (c[4] << 8);
  }
  ESP_LOGI(kTag, "product %s at 0x%02X, config v%d, area %dx%d, INT pin %d", id, address, c[0], max_x_, max_y_,
           gpio_get_level(irq));
  return true;
}

bool Gt911::attach(i2c_master_bus_handle_t bus, uint32_t scl_hz, int rst_gpio, int int_gpio) {
  int_gpio_ = int_gpio;
  // A controller that has only just been powered may miss the first reset:
  // try both addresses a few times before giving up.
  for (int attempt = 0; attempt < 3; ++attempt) {
    for (uint8_t address : {kAddress, kAltAddress}) {
      if (reset_and_probe(bus, scl_hz, rst_gpio, int_gpio, address)) {
        write_reg(kRegStatus, 0);
        return true;
      }
    }
    delay_ms(50);
  }
  return false;
}

bool Gt911::read(Report& out) {
  if (dev_ == nullptr) return false;
  uint8_t status;
  if (!read_reg(kRegStatus, &status, 1) || !(status & kReady)) return false;
  const int fingers = status & 0x0F;
  out.home = (status & kKey) != 0;
  out.touching = fingers > 0 && fingers <= 5;
  if (out.touching) {
    uint8_t p[4];
    if (read_reg(kRegPoint1, p, sizeof(p))) {
      out.x = p[0] | (p[1] << 8);
      out.y = p[2] | (p[3] << 8);
    } else {
      out.touching = false;
    }
  }
  write_reg(kRegStatus, 0);  // hand the buffer back to the controller
  return true;
}

void Gt911::sleep() {
  if (dev_ == nullptr) return;
  // The controller takes the command only while its interrupt line is low.
  const auto irq = static_cast<gpio_num_t>(int_gpio_);
  gpio_intr_disable(irq);
  gpio_set_direction(irq, GPIO_MODE_OUTPUT);
  gpio_set_level(irq, 0);
  delay_ms(1);
  if (!write_reg(kRegCommand, 0x05)) ESP_LOGW(kTag, "sleep command not taken");
}

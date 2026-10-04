// Boot-time controller detection, ported from FreeInk's XteinkDetect (MIT).
#include <cstring>

#include "driver/gpio.h"
#include "epd_controller.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace epd {
namespace {

constexpr const char* kTag = "epd";
constexpr uint8_t kCmdVer = 0x70;   // reserved, CHIP_VER, LUT_VER[23:0]
constexpr uint8_t kCmdFlg = 0x71;   // status; bit 0 (BUSY_N) is 1 when idle
constexpr uint8_t kCmdRmtp = 0xA2;  // MTP read: one dummy byte, then MTP[0..]
constexpr size_t kMtpLen = 48;

void out(int pin, int level) { gpio_set_level(static_cast<gpio_num_t>(pin), level); }
int in(int pin) { return gpio_get_level(static_cast<gpio_num_t>(pin)); }
void tick() { esp_rom_delay_us(1); }  // about 500 kHz

void mode(int pin, gpio_mode_t m, gpio_pull_mode_t pull = GPIO_FLOATING) {
  gpio_set_direction(static_cast<gpio_num_t>(pin), m);
  gpio_set_pull_mode(static_cast<gpio_num_t>(pin), pull);
}

void write_byte(const Pins& p, uint8_t b) {
  for (int i = 0; i < 8; ++i) {
    out(p.mosi, (b & 0x80) ? 1 : 0);
    tick();
    out(p.sclk, 1);
    tick();
    out(p.sclk, 0);
    b = static_cast<uint8_t>(b << 1);
  }
}

uint8_t read_byte(const Pins& p) {
  uint8_t b = 0;
  for (int i = 0; i < 8; ++i) {
    tick();
    b = static_cast<uint8_t>((b << 1) | (in(p.mosi) ? 1 : 0));
    out(p.sclk, 1);
    tick();
    out(p.sclk, 0);
  }
  return b;
}

// The controller answers on the same data line (half duplex), so MOSI turns
// into an input with a pull-up for the reply.
void cmd_read(const Pins& p, uint8_t cmd, uint8_t* dst, size_t n) {
  mode(p.mosi, GPIO_MODE_OUTPUT);
  out(p.dc, 0);
  out(p.cs, 0);
  tick();
  write_byte(p, cmd);
  out(p.dc, 1);
  mode(p.mosi, GPIO_MODE_INPUT, GPIO_PULLUP_ONLY);
  tick();
  for (size_t i = 0; i < n; ++i) dst[i] = read_byte(p);
  out(p.cs, 1);
  mode(p.mosi, GPIO_MODE_OUTPUT);
}

bool ver_floating(const uint8_t ver[5]) {
  for (int i = 1; i < 5; ++i)
    if (ver[i] != ver[0]) return false;
  return true;
}

bool flg_driven(uint8_t flg) { return flg != 0x00 && flg != 0xFF && (flg & 0x01) == 0x01; }

bool probe_pass(const Pins& p, uint8_t ver[5], uint8_t* flg, uint32_t rst_low_ms) {
  for (int pin : {p.cs, p.sclk, p.dc, p.mosi, p.rst}) {
    gpio_reset_pin(static_cast<gpio_num_t>(pin));
    mode(pin, GPIO_MODE_OUTPUT);
  }
  gpio_reset_pin(static_cast<gpio_num_t>(p.busy));
  mode(p.busy, GPIO_MODE_INPUT);
  out(p.cs, 1);
  out(p.sclk, 0);
  out(p.dc, 0);

  out(p.rst, 1);
  Bus::delay_ms(2);
  out(p.rst, 0);
  Bus::delay_ms(rst_low_ms);
  out(p.rst, 1);
  Bus::delay_ms(30);

  uint8_t f = 0;
  cmd_read(p, kCmdFlg, &f, 1);
  cmd_read(p, kCmdVer, ver, 5);
  if (flg) *flg = f;
  return flg_driven(f) && !ver_floating(ver);
}

void release(const Pins& p) {
  mode(p.sclk, GPIO_MODE_INPUT);
  mode(p.mosi, GPIO_MODE_INPUT);
  mode(p.cs, GPIO_MODE_INPUT, GPIO_PULLUP_ONLY);  // keep the panel deselected
  mode(p.dc, GPIO_MODE_INPUT);
  mode(p.rst, GPIO_MODE_INPUT);
}

bool is_uc8279_id(uint8_t id) {
  return id == 0x02 || id == 0x03 || id == 0x41 || id == 0x42 || id == 0x67 || id == 0x68 || id == 0x69;
}

}  // namespace

ProbeResult probe(const Pins& p) {
  uint8_t ver1[5] = {};
  uint8_t ver2[5] = {};
  uint8_t flg = 0;
  const bool pass1 = probe_pass(p, ver1, &flg, 1);
  Bus::delay_ms(2);
  // A slower second reset confirms a real reply rather than bus noise.
  const bool pass2 = probe_pass(p, ver2, nullptr, pass1 ? 50 : 1);
  const bool agree = memcmp(ver1, ver2, 5) == 0;
  bool uc81xx = pass1 && pass2 && agree;

  // Some UltraChip parts return an all-0xFF VER but still drive FLG; a stable,
  // non-blank MTP read (or the 0xA5 MTP signature) confirms them.
  if (!uc81xx && flg_driven(flg) && agree && ver_floating(ver1) && ver1[0] == 0xFF) {
    uint8_t mtp1[kMtpLen + 1] = {};
    cmd_read(p, kCmdRmtp, mtp1, sizeof(mtp1));
    if (mtp1[1] == 0xA5) {
      uc81xx = true;
    } else {
      bool uniform = true;
      for (size_t i = 2; i < sizeof(mtp1); ++i)
        if (mtp1[i] != mtp1[1]) uniform = false;
      if (!uniform) {
        uint8_t mtp2[kMtpLen + 1] = {};
        cmd_read(p, kCmdRmtp, mtp2, sizeof(mtp2));
        uc81xx = memcmp(mtp1 + 1, mtp2 + 1, kMtpLen) == 0;
      }
    }
  }
  release(p);

  ProbeResult r;
  memcpy(r.ver, pass1 && pass2 ? ver2 : ver1, 5);
  r.flg = flg;
  if (uc81xx) {
    // Unrecognised UltraChip ids default to the UC8179 path, as FreeInk does.
    r.chip = is_uc8279_id(r.ver[2]) ? Chip::Uc8279 : Chip::Uc8179;
  }
  ESP_LOGI(kTag, "probe VER=%02X %02X %02X %02X %02X FLG=%02X", r.ver[0], r.ver[1], r.ver[2], r.ver[3], r.ver[4],
           r.flg);
  return r;
}

}  // namespace epd

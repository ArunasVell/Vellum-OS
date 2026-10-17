// E-paper controller drivers for the X4 Pro panel batches.
//
// The X4 Pro ships the same 800x480 glass with one of three controllers:
// Solomon SSD1677, UltraChip UC8179 or UltraChip UC8279. probe() tells them
// apart at boot. Register sequences are ported from the FreeInk SDK (MIT, see
// NOTICE), which validated them on hardware.
//
// Frames are 1 bit per pixel, row-major in panel orientation, MSB = leftmost
// pixel, 1 = white: the same layout as vellum::Framebuffer.
#pragma once

#include <cstdint>

#include "epd_bus.h"

namespace epd {

enum class Mode : uint8_t {
  Fast,  // differential update, no flash
  Half,  // clears ghosting with less flashing than Full
  Full,  // full flash from white
};

class Controller {
 public:
  virtual ~Controller() = default;
  virtual const char* name() const = 0;
  virtual uint32_t spi_hz() const = 0;
  // Resets and initialises the controller.
  virtual void begin(Bus& bus) = 0;
  // Shows `frame`, blocking until the refresh completes. With `power_off`
  // the panel's high-voltage supply is switched off afterwards.
  virtual void display(Bus& bus, const uint8_t* frame, Mode mode, bool power_off) = 0;
  // Switches the high-voltage supply off, when a display() left it on. The
  // next display() switches it on again, which takes time: leaving it on
  // for a moment makes quick page turns faster.
  virtual void power_off(Bus& bus) = 0;
  // Lowest-power state; begin() is required before the next display().
  virtual void deep_sleep(Bus& bus) = 0;
};

enum class Chip : uint8_t { Ssd1677, Uc8179, Uc8279 };

struct ProbeResult {
  Chip chip = Chip::Ssd1677;
  uint8_t ver[5] = {};  // UC81xx VER (0x70) reply; ver[2] is the LUT version id
  uint8_t flg = 0;      // UC81xx FLG (0x71) reply
};

// Identifies the controller by bit-banging the UltraChip status/version reads
// on the display pins. Must run before Bus::init() claims the pins for SPI.
// SSD1677 does not answer these reads, so it is the fallback.
ProbeResult probe(const Pins& pins);

Controller& ssd1677(uint16_t width, uint16_t height);
Controller& uc8179(uint16_t width, uint16_t height);
Controller& uc8279(uint16_t width, uint16_t height);

}  // namespace epd

// Hardware API: the only interface between portable code and a device.
//
// Everything above this header (UI, reader, services) is device-agnostic.
// A board (real hardware or the desktop simulator) implements these
// interfaces and hands them to vellum::run() as a Board.
#pragma once

#include <cstdint>

#include "vellum/framebuffer.h"

namespace vellum {

enum class RefreshMode : uint8_t {
  Fast,  // differential waveform, no flash; may leave ghosting
  Full,  // full waveform with flash; clears ghosting
};

class DisplayPanel {
 public:
  virtual ~DisplayPanel() = default;
  virtual int width() const = 0;   // physical pixels
  virtual int height() const = 0;  // physical pixels
  // Sends the framebuffer to the panel and refreshes. `dirty` is the changed
  // area in physical coordinates; drivers may refresh more than that.
  // Drivers power the controller down when the refresh completes.
  virtual void present(const Framebuffer& fb, RefreshMode mode, Rect dirty) = 0;
};

enum class Key : uint8_t { Prev, Next, Home, Power };

struct InputEvent {
  enum class Type : uint8_t { None, KeyDown, KeyUp, Quit };
  Type type = Type::None;
  Key key = Key::Next;
};

class InputSource {
 public:
  virtual ~InputSource() = default;
  // Blocks (sleeping where possible) until an event arrives or the timeout
  // passes. Returns false on timeout.
  virtual bool wait(InputEvent& out, uint32_t timeout_ms) = 0;
};

class Power {
 public:
  virtual ~Power() = default;
  virtual int battery_percent() = 0;  // 0..100, or -1 when unknown
  virtual bool charging() = 0;
};

class Frontlight {
 public:
  virtual ~Frontlight() = default;
  // level 0 = off, 255 = brightest; warmth 0 = coolest, 255 = warmest.
  virtual void set(uint8_t level, uint8_t warmth) = 0;
};

struct DateTime {
  uint16_t year;   // e.g. 2026
  uint8_t month;   // 1..12
  uint8_t day;     // 1..31
  uint8_t hour;    // 0..23
  uint8_t minute;  // 0..59
  uint8_t second;  // 0..59
};

class Clock {
 public:
  virtual ~Clock() = default;
  // Local wall-clock time. Returns false when the time is not known yet
  // (the clock lost power and has not been set since).
  virtual bool now(DateTime& out) = 0;
};

struct Board {
  const char* name;
  DisplayPanel& display;
  InputSource& input;
  Power& power;
  Frontlight* frontlight;  // nullptr when the device has none
  Clock* clock;            // nullptr when the device has none
  Rotation rotation;       // logical orientation for reading
  uint8_t* framebuffer;    // Framebuffer::bytes_for(display size) bytes
};

}  // namespace vellum

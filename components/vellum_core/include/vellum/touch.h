// Turns raw touch samples into taps, swipes and long presses.
#pragma once

#include <cstdint>

#include "vellum/framebuffer.h"
#include "vellum/hal.h"

namespace vellum {

struct Gesture {
  enum class Kind : uint8_t { None, Tap, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, LongPress };
  Kind kind = Kind::None;
  int x = 0;  // logical point where the finger went down
  int y = 0;
};

class TouchTracker {
 public:
  // A finger that moves less than this is a tap.
  static constexpr int kTapSlop = 28;
  // A swipe travels at least this far, mostly along one axis.
  static constexpr int kSwipeMin = 64;
  // A finger held still this long is a long press.
  static constexpr uint32_t kLongPressMs = 600;

  // Feeds one input event; returns the gesture it completes, if any.
  // Touch events not followed by a TouchUp (a lost release) are dropped when
  // the next TouchDown arrives. A long press is reported once, as soon as an
  // event shows the finger has been still for kLongPressMs (boards that keep
  // reporting a resting finger), or on release; the release after it is not
  // a tap.
  Gesture feed(const InputEvent& ev, const Framebuffer& fb);

  // A finger is down, still enough and not yet reported as a long press:
  // a host that sees no event for kLongPressMs calls hold().
  bool holding() const { return down_ && !held_ && travel_ <= kTapSlop; }
  // The long press of a finger that has rested since it went down without
  // events (the simulators); None when it is not holding().
  Gesture hold();

 private:
  bool down_ = false;
  bool held_ = false;  // the long press was reported
  int x0_ = 0;
  int y0_ = 0;
  int x_ = 0;
  int y_ = 0;
  int travel_ = 0;  // farthest distance from the start, either axis
  uint32_t down_ms_ = 0;
  bool timed_ = false;  // the press carried a time
};

}  // namespace vellum

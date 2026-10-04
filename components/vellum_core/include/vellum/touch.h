// Turns raw touch samples into taps and swipes.
#pragma once

#include <cstdint>

#include "vellum/framebuffer.h"
#include "vellum/hal.h"

namespace vellum {

struct Gesture {
  enum class Kind : uint8_t { None, Tap, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };
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

  // Feeds one input event; returns the gesture it completes, if any.
  // Touch events not followed by a TouchUp (a lost release) are dropped when
  // the next TouchDown arrives.
  Gesture feed(const InputEvent& ev, const Framebuffer& fb);

 private:
  bool down_ = false;
  int x0_ = 0;
  int y0_ = 0;
  int x_ = 0;
  int y_ = 0;
  int travel_ = 0;  // farthest distance from the start, either axis
};

}  // namespace vellum

#include "vellum/touch.h"

#include <algorithm>
#include <cstdlib>

namespace vellum {

Gesture TouchTracker::hold() {
  if (!holding()) return {};
  held_ = true;
  Gesture g;
  g.kind = Gesture::Kind::LongPress;
  g.x = x0_;
  g.y = y0_;
  return g;
}

Gesture TouchTracker::feed(const InputEvent& ev, const Framebuffer& fb) {
  using T = InputEvent::Type;
  if (ev.type != T::TouchDown && ev.type != T::TouchMove && ev.type != T::TouchUp) return {};
  int x, y;
  fb.to_logical(ev.x, ev.y, x, y);
  if (ev.type == T::TouchDown) {
    down_ = true;
    held_ = false;
    x0_ = x_ = x;
    y0_ = y_ = y;
    travel_ = 0;
    down_ms_ = ev.ms;
    timed_ = ev.ms != 0;
    return {};
  }
  if (!down_) return {};
  x_ = x;
  y_ = y;
  const int dx = x_ - x0_;
  const int dy = y_ - y0_;
  travel_ = std::max({travel_, std::abs(dx), std::abs(dy)});
  const bool long_enough = timed_ && ev.ms != 0 && ev.ms - down_ms_ >= kLongPressMs;
  if (ev.type == T::TouchMove) return long_enough ? hold() : Gesture{};

  down_ = false;
  if (held_) return {};  // the long press already said everything
  Gesture g;
  g.x = x0_;
  g.y = y0_;
  if (travel_ <= kTapSlop) {
    g.kind = long_enough ? Gesture::Kind::LongPress : Gesture::Kind::Tap;
  } else if (std::abs(dx) >= kSwipeMin && std::abs(dx) > 2 * std::abs(dy)) {
    g.kind = dx < 0 ? Gesture::Kind::SwipeLeft : Gesture::Kind::SwipeRight;
  } else if (std::abs(dy) >= kSwipeMin && std::abs(dy) > 2 * std::abs(dx)) {
    g.kind = dy < 0 ? Gesture::Kind::SwipeUp : Gesture::Kind::SwipeDown;
  }
  return g;  // anything else (a wander, a diagonal) does nothing
}

}  // namespace vellum

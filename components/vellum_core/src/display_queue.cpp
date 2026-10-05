#include "vellum/display_queue.h"

namespace vellum {

DisplayQueue::DisplayQueue(DisplayPanel& panel, Framebuffer& fb, RefreshPolicy policy)
    : panel_(panel), fb_(fb), policy_(policy) {}

RefreshMode DisplayQueue::submit() {
  const Rect dirty = fb_.dirty();
  if (dirty.empty()) return RefreshMode::Fast;

  if (full_requested_ || policy_.always_full || (policy_.full_every != 0 && fast_since_full_ >= policy_.full_every)) {
    submit_full();
    return RefreshMode::Full;
  }
  panel_.present(fb_, RefreshMode::Fast, dirty);
  fb_.clear_dirty();
  ++fast_since_full_;
  return RefreshMode::Fast;
}

void DisplayQueue::submit_full() {
  fb_.mark_all_dirty();
  panel_.present(fb_, RefreshMode::Full, fb_.dirty());
  fb_.clear_dirty();
  fast_since_full_ = 0;
  full_requested_ = false;
}

}  // namespace vellum

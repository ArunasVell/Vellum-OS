// Owns the decision of how to refresh the panel.
//
// Apps draw into the framebuffer and call submit(); the queue sends only the
// dirty region and picks fast or full refresh so ghosting is cleaned up
// periodically without flashing on every page turn.
#pragma once

#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

struct RefreshPolicy {
  // A full refresh runs after this many fast ones. 0 = never automatically.
  uint16_t full_every = 12;
  // Every refresh is a full one.
  bool always_full = false;

  bool operator==(const RefreshPolicy&) const = default;
};

class DisplayQueue {
 public:
  DisplayQueue(DisplayPanel& panel, Framebuffer& fb, RefreshPolicy policy = {});

  // Presents pending changes. Returns the mode used, or Fast with nothing sent
  // when there was nothing to draw.
  RefreshMode submit();
  // Presents the whole screen with a full refresh and restarts the cadence.
  void submit_full();

  // The next submit() is a full refresh, whatever the cadence says.
  void request_full() { full_requested_ = true; }

  void set_policy(RefreshPolicy p) { policy_ = p; }
  RefreshPolicy policy() const { return policy_; }
  uint16_t fast_since_full() const { return fast_since_full_; }

 private:
  DisplayPanel& panel_;
  Framebuffer& fb_;
  RefreshPolicy policy_;
  uint16_t fast_since_full_ = 0;
  bool full_requested_ = false;
};

}  // namespace vellum

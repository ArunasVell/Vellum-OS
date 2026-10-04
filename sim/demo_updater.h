// Updates in the simulators: there is no network to join, so this one
// pretends. A home network is always set, a check always finds a newer
// version, and installing it counts up and "installs" nothing.
#pragma once

#include <cstdio>

#include "vellum/hal.h"

class DemoUpdater : public vellum::Updater {
 public:
  void home_network(char* out, size_t size) override { std::snprintf(out, size, "Home (simulated)"); }

  vellum::UpdateResult check(const char*, char* latest, size_t size) override {
    std::snprintf(latest, size, "0.1.99");
    return vellum::UpdateResult::Available;
  }

  vellum::UpdateResult install(ProgressFn progress, void* ctx) override {
    for (int percent = 0; percent <= 100; percent += 10) {
      if (progress != nullptr) progress(ctx, percent);
    }
    return vellum::UpdateResult::Installed;
  }
};

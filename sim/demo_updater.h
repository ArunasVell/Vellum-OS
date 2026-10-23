// Updates in the simulators: there is no network to join, so this one
// pretends. A home network is always set, a check always finds a newer
// version (with release notes, as a release page has them), and installing
// it counts up and "installs" nothing.
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

  void notes(char* out, size_t size) override {
    std::snprintf(out, size, "%s",
                  "First install: download the installer and open it in Chrome or Edge.\n\n"
                  "## New in this version\n\n"
                  "### New features\n\n"
                  "* Show what's new before and after an update (#60)\n"
                  "* Open PDFs much faster, with progress and a way to stop (#56)\n"
                  "* Even out menu text sizes across fonts; make Normal bigger (#52)\n"
                  "* Faster page turns and longer battery life (#48)\n"
                  "* Ten menu languages, and a new Settings list grouped by topic (#49)\n\n"
                  "### Bug fixes\n\n"
                  "* Landscape: say it turns books only, and fix which side the keys go (#51)\n"
                  "* Fix the free book store listing the same books twice (#49)\n"
                  "* Fix the touch screen not answering on the X4 Pro (#37)\n\n"
                  "### Other changes\n\n"
                  "* Add a troubleshooting guide (#58)\n"
                  "* README: newer features, Settings pages and the ten menu languages (#57)\n");
  }

  vellum::UpdateResult install(ProgressFn progress, void* ctx) override {
    for (int percent = 0; percent <= 100; percent += 10) {
      if (progress != nullptr) progress(ctx, percent);
    }
    return vellum::UpdateResult::Installed;
  }
};

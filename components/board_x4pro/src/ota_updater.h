// New versions of Vellum from GitHub Releases: joins the home Wi-Fi, asks
// for the latest release and installs its .bin into the spare app slot.
#pragma once

#include "vellum/hal.h"

class OtaUpdater : public vellum::Updater {
 public:
  void home_network(char* out, size_t size) override;
  vellum::UpdateResult check(const char* current, char* latest, size_t size) override;
  vellum::UpdateResult install(ProgressFn progress, void* ctx) override;

 private:
  char url_[512] = {};  // the newest release's .bin, from the last check
};

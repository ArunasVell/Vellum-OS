// New versions of Vellum from GitHub Releases: joins the home Wi-Fi, asks
// for the latest release and installs its .bin into the spare app slot.
#pragma once

#include "vellum/hal.h"

class OtaUpdater : public vellum::Updater {
 public:
  void home_network(char* out, size_t size) override;
  vellum::UpdateResult check(const char* current, char* latest, size_t size) override;
  vellum::UpdateResult install(ProgressFn progress, void* ctx) override;
  void notes(char* out, size_t size) override;

 private:
  char url_[512] = {};      // the newest release's .bin, from the last check
  char* answer_ = nullptr;  // the server's last answer (in PSRAM), kept for its notes
  size_t answer_len_ = 0;   // 0 when the last check found nothing to install
};

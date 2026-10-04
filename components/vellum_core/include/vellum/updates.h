// Software updates: comparing version numbers, reading the release
// server's answer, and what the reader remembers between checks
// (vellum/update.txt on the card).
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

// True when `latest` is a newer release than `current`. Releases are
// numbered like "0.1.42" (a leading "v" is ignored); a `current` that is not
// a release number (a build of one's own, "dev") counts as older than any
// release.
bool is_newer_version(const char* latest, const char* current);

// Reads a GitHub "latest release" answer (JSON): its tag as the version
// (without the "v") and the download address of its .bin file. Returns
// false when either is missing.
bool parse_release(const char* json, size_t len, char* version, size_t version_size, char* url, size_t url_size);

// Seconds from `now` until the next time the clock shows hour:00, 1..86400.
uint32_t seconds_until_hour(const DateTime& now, int hour);

// The nightly check runs at this hour, while the reader sleeps.
constexpr int kNightHour = 3;

struct UpdateState {
  char latest[32] = {};        // newest version seen on the server, or ""
  DateTime checked = {};       // when it was last asked
  bool has_checked = false;    // `checked` is set
  bool night_restart = false;  // a version was installed at night: go back to sleep after the restart
};

constexpr const char* kUpdateStatePath = "vellum/update.txt";
void load_update_state(Storage& storage, UpdateState& out);
bool save_update_state(Storage& storage, const UpdateState& s);

}  // namespace vellum

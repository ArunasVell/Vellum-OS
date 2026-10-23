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

// The release notes in a GitHub "latest release" answer: its "body" string,
// unescaped into UTF-8 (cut at a whole line when it does not fit). Returns
// false when there is none.
bool release_body(const char* json, size_t len, char* out, size_t size);

// Release notes in Markdown (a release's body, or the notes CI builds into
// the firmware) made ready for the "What's new" screen: only what follows a
// "## New in this version" line when there is one, then one entry per line,
// "#Group" for a "### Group" heading and the change itself for a "* " or
// "- " line, without its " (#N)". Everything else is dropped. `out` may be
// `md` itself. Returns the length written (out is 0-terminated).
size_t tidy_release_notes(const char* md, size_t len, char* out, size_t size);

// The notes of the version found but not installed yet (UpdateState::latest),
// tidied as above.
constexpr const char* kReleaseNotesPath = "vellum/notes.txt";

// Seconds from `now` until the next time the clock shows hour:00, 1..86400.
uint32_t seconds_until_hour(const DateTime& now, int hour);

// The nightly check runs at this hour, while the reader sleeps.
constexpr int kNightHour = 3;

struct UpdateState {
  char latest[32] = {};        // newest version seen on the server, or ""
  DateTime checked = {};       // when it was last asked
  bool has_checked = false;    // `checked` is set
  bool night_restart = false;  // a version was installed at night: go back to sleep after the restart
  char started[32] = {};       // the newest version that has started with this card; another shows What's new
};

constexpr const char* kUpdateStatePath = "vellum/update.txt";
void load_update_state(Storage& storage, UpdateState& out);
bool save_update_state(Storage& storage, const UpdateState& s);

}  // namespace vellum

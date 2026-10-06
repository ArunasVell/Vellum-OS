// User preferences, saved on the card.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"
#include "vellum/strings.h"

namespace vellum {

enum class TimeFormat : uint8_t {
  H24,  // 14:05
  H12,  // 2:05 PM
};

enum class TextSize : uint8_t { Small, Medium, Large, ExtraLarge };
enum class Spacing : uint8_t { Narrow, Normal, Wide };
enum class SleepAfter : uint8_t { Min5, Min10, Min30, Never };
// New versions: installed at night, only announced, or not looked for.
enum class AutoUpdate : uint8_t { AtNight, AskFirst, Off };
// What the middle of the reader's footer shows.
enum class FooterInfo : uint8_t { PageNumber, ChapterTime, Percent };
// The order of the library's books: recently read, title, author, or
// recently added to the card.
enum class LibrarySort : uint8_t { Recent, Title, Author, Added };
// How home and the library show books: rows, or a grid of covers.
enum class LibraryView : uint8_t { List, Covers };
constexpr int kLightSteps = 10;  // front light brightness and warmth go 0..10

struct Settings {
  Language language = Language::English;
  TimeFormat time_format = TimeFormat::H24;
  SleepAfter sleep_after = SleepAfter::Min10;  // idle time before the reader sleeps
  uint8_t light = 0;                           // front light, 0 (off) to kLightSteps
  uint8_t warmth = 3;                          // 0 (coolest) to kLightSteps (warmest)
  AutoUpdate auto_update = AutoUpdate::AtNight;
  LibrarySort library_sort = LibrarySort::Recent;
  // "Add books" opens the reader's own Wi-Fi network even when a home
  // Wi-Fi is set (else it serves the page there).
  bool own_network = false;
  LibraryView library_view = LibraryView::List;
  bool move_finished = false;  // finished books move to books/Read/
  // Reading
  TextSize text_size = TextSize::Medium;
  Spacing line_spacing = Spacing::Normal;
  Spacing word_spacing = Spacing::Normal;
  Spacing margins = Spacing::Normal;
  bool justify = true;
  bool hyphenate = true;
  FooterInfo footer = FooterInfo::PageNumber;
  // Typefaces by family name (vellum/font_library.h); a missing one falls back to Literata.
  char book_font[48] = "Literata";
  char menu_font[48] = "Literata";

  bool operator==(const Settings&) const = default;
};

// Settings live in vellum/settings.txt on the card as key=value lines;
// unknown keys and values are ignored, so older files keep working.
constexpr const char* kSettingsPath = "vellum/settings.txt";
void load_settings(Storage& storage, Settings& out);
bool save_settings(Storage& storage, const Settings& s);

// The phone page's Settings: the settings as JSON, under the keys of
// settings.txt, with `fonts` (one typeface per line, or nullptr) as a list:
// {"language":0,...,"book_font":"Literata","menu_font":"Literata","fonts":[...]}.
// Returns false when it does not fit.
bool settings_json(const Settings& s, const char* fonts, char* out, size_t size);
// Sets the settings named in a form body ("text_size=2&justify=0"), as
// settings.txt names them. Returns false, changing nothing, when a value
// is out of range or nothing is named.
bool settings_from_form(const char* body, size_t len, Settings& s);

// Idle time before sleeping, in milliseconds; 0 for never.
uint32_t sleep_after_ms(SleepAfter s);

// Writes the time of day in `format` into out (at least 9 bytes).
void format_time(const DateTime& t, TimeFormat format, char* out, size_t size);

}  // namespace vellum

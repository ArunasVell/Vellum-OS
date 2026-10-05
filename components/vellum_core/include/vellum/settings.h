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
// The order of the library's books.
enum class LibrarySort : uint8_t { Recent, Title, Author };
constexpr int kLightSteps = 10;  // front light brightness and warmth go 0..10

struct Settings {
  Language language = Language::English;
  TimeFormat time_format = TimeFormat::H24;
  SleepAfter sleep_after = SleepAfter::Min10;  // idle time before the reader sleeps
  uint8_t light = 0;                           // front light, 0 (off) to kLightSteps
  uint8_t warmth = 3;                          // 0 (coolest) to kLightSteps (warmest)
  AutoUpdate auto_update = AutoUpdate::AtNight;
  LibrarySort library_sort = LibrarySort::Recent;
  // Reading
  TextSize text_size = TextSize::Medium;
  Spacing line_spacing = Spacing::Normal;
  Spacing word_spacing = Spacing::Normal;
  Spacing margins = Spacing::Normal;
  bool justify = true;
  bool hyphenate = true;
  FooterInfo footer = FooterInfo::PageNumber;
  bool night = false;            // the reader's page white on black
  bool focus_reading = false;    // the first part of each word in bold
  bool publisher_styles = true;  // the book's own alignment, indents and emphasis
  uint8_t paragraph_indent = 0;  // IndentRule: 0 as the book has it, 1 always, 2 never
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

// Idle time before sleeping, in milliseconds; 0 for never.
uint32_t sleep_after_ms(SleepAfter s);

// Writes the time of day in `format` into out (at least 9 bytes).
void format_time(const DateTime& t, TimeFormat format, char* out, size_t size);

}  // namespace vellum

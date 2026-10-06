// User preferences, saved on the card.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/controls.h"
#include "vellum/display_queue.h"
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
// Which way up the screen is. Upside down turns every screen; landscape
// turns the reader only (menus stay upright, as they are laid out for it).
enum class Orientation : uint8_t { Portrait, UpsideDown, LandscapeLeft, LandscapeRight };
// How often a full refresh clears the reader's ghosting: after every page,
// after 5, 12 (as always) or 20 fast ones, or only at a new chapter.
enum class FullRefresh : uint8_t { EveryPage, Every5, Every12, Every20, NewChapter };
// Seconds between pages for automatic page turning, by Settings::auto_turn.
constexpr uint8_t kAutoTurnSeconds[] = {5, 10, 15, 20, 30, 45, 60, 90, 120};
constexpr int kAutoTurnSteps = sizeof(kAutoTurnSeconds);
// What the screen shows while the reader sleeps (Settings › Sleep screen).
enum class SleepScreen : uint8_t { Cover, Pictures, Stats, Minimal, Page };
// Which of the reader's own pictures: a different one each time, or one kept.
enum class SleepOrder : uint8_t { Shuffle, One };
constexpr int kLightSteps = 10;  // front light brightness and warmth go 0..10

struct Settings {
  Language language = Language::English;
  TimeFormat time_format = TimeFormat::H24;
  SleepAfter sleep_after = SleepAfter::Min10;  // idle time before the reader sleeps
  uint8_t light = 0;                           // front light, 0 (off) to kLightSteps
  uint8_t warmth = 3;                          // 0 (coolest) to kLightSteps (warmest)
  AutoUpdate auto_update = AutoUpdate::AtNight;
  LibrarySort library_sort = LibrarySort::Recent;
  SleepScreen sleep_screen = SleepScreen::Cover;
  SleepOrder sleep_order = SleepOrder::Shuffle;
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
  uint8_t auto_turn = 4;  // index into kAutoTurnSeconds: 30 s
  FullRefresh full_refresh = FullRefresh::Every12;
  // Controls
  Orientation orientation = Orientation::Portrait;
  ButtonMap buttons = kDefaultButtons;
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

// The reader's refresh cadence for Settings::full_refresh. NewChapter
// never asks for one itself: the app does at each new chapter.
RefreshPolicy refresh_policy(FullRefresh f);

// Writes the time of day in `format` into out (at least 9 bytes).
void format_time(const DateTime& t, TimeFormat format, char* out, size_t size);

}  // namespace vellum

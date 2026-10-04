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

struct Settings {
  Language language = Language::English;
  TimeFormat time_format = TimeFormat::H24;
  SleepAfter sleep_after = SleepAfter::Min10;  // idle time before the reader sleeps
  // Reading
  TextSize text_size = TextSize::Medium;
  Spacing line_spacing = Spacing::Normal;
  Spacing word_spacing = Spacing::Normal;
  Spacing margins = Spacing::Normal;
  bool justify = true;
  bool hyphenate = true;

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

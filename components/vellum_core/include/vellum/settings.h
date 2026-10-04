// User preferences. Kept in RAM for now; saved to storage once the SD card
// driver lands, together with reading progress.
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

struct Settings {
  Language language = Language::English;
  TimeFormat time_format = TimeFormat::H24;
  // Reading
  TextSize text_size = TextSize::Medium;
  Spacing line_spacing = Spacing::Normal;
  Spacing word_spacing = Spacing::Normal;
  Spacing margins = Spacing::Normal;
  bool justify = true;
  bool hyphenate = true;

  bool operator==(const Settings&) const = default;
};

// Writes the time of day in `format` into out (at least 9 bytes).
void format_time(const DateTime& t, TimeFormat format, char* out, size_t size);

}  // namespace vellum

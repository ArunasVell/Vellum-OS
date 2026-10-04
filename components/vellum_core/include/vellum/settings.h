// User preferences. Kept in RAM for now; saved to storage once the SD card
// driver lands, together with reading progress.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

enum class TimeFormat : uint8_t {
  H24,  // 14:05
  H12,  // 2:05 PM
};

struct Settings {
  TimeFormat time_format = TimeFormat::H24;
};

// Writes the time of day in `format` into out (at least 9 bytes).
void format_time(const DateTime& t, TimeFormat format, char* out, size_t size);

}  // namespace vellum

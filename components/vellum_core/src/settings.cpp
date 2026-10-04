#include "vellum/settings.h"

#include <cstdio>

namespace vellum {

void format_time(const DateTime& t, TimeFormat format, char* out, size_t size) {
  if (format == TimeFormat::H24) {
    std::snprintf(out, size, "%02u:%02u", unsigned{t.hour}, unsigned{t.minute});
    return;
  }
  const unsigned hour = t.hour % 12 == 0 ? 12 : t.hour % 12;
  std::snprintf(out, size, "%u:%02u %s", hour, unsigned{t.minute}, t.hour < 12 ? "AM" : "PM");
}

}  // namespace vellum

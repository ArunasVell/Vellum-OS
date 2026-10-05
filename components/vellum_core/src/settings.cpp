#include "vellum/settings.h"

#include <cstdio>
#include <string_view>

#include "vellum/textfile.h"

namespace vellum {

void format_time(const DateTime& t, TimeFormat format, char* out, size_t size) {
  if (format == TimeFormat::H24) {
    std::snprintf(out, size, "%02u:%02u", unsigned{t.hour}, unsigned{t.minute});
    return;
  }
  const unsigned hour = t.hour % 12 == 0 ? 12 : t.hour % 12;
  std::snprintf(out, size, "%u:%02u %s", hour, unsigned{t.minute}, t.hour < 12 ? "AM" : "PM");
}

namespace {

struct SettingKey {
  const char* name;
  int count;
  int (*get)(const Settings&);
  void (*set)(Settings&, int);
};

constexpr SettingKey kKeys[] = {
    {"language", 2, [](const Settings& s) { return static_cast<int>(s.language); },
     [](Settings& s, int v) { s.language = static_cast<Language>(v); }},
    {"clock", 2, [](const Settings& s) { return static_cast<int>(s.time_format); },
     [](Settings& s, int v) { s.time_format = static_cast<TimeFormat>(v); }},
    {"sleep_after", 4, [](const Settings& s) { return static_cast<int>(s.sleep_after); },
     [](Settings& s, int v) { s.sleep_after = static_cast<SleepAfter>(v); }},
    {"light", kLightSteps + 1, [](const Settings& s) { return int{s.light}; },
     [](Settings& s, int v) { s.light = static_cast<uint8_t>(v); }},
    {"warmth", kLightSteps + 1, [](const Settings& s) { return int{s.warmth}; },
     [](Settings& s, int v) { s.warmth = static_cast<uint8_t>(v); }},
    {"updates", 3, [](const Settings& s) { return static_cast<int>(s.auto_update); },
     [](Settings& s, int v) { s.auto_update = static_cast<AutoUpdate>(v); }},
    {"library_sort", 3, [](const Settings& s) { return static_cast<int>(s.library_sort); },
     [](Settings& s, int v) { s.library_sort = static_cast<LibrarySort>(v); }},
    {"text_size", 4, [](const Settings& s) { return static_cast<int>(s.text_size); },
     [](Settings& s, int v) { s.text_size = static_cast<TextSize>(v); }},
    {"line_spacing", 3, [](const Settings& s) { return static_cast<int>(s.line_spacing); },
     [](Settings& s, int v) { s.line_spacing = static_cast<Spacing>(v); }},
    {"word_spacing", 3, [](const Settings& s) { return static_cast<int>(s.word_spacing); },
     [](Settings& s, int v) { s.word_spacing = static_cast<Spacing>(v); }},
    {"margins", 3, [](const Settings& s) { return static_cast<int>(s.margins); },
     [](Settings& s, int v) { s.margins = static_cast<Spacing>(v); }},
    {"justify", 2, [](const Settings& s) { return s.justify ? 1 : 0; }, [](Settings& s, int v) { s.justify = v != 0; }},
    {"hyphenate", 2, [](const Settings& s) { return s.hyphenate ? 1 : 0; },
     [](Settings& s, int v) { s.hyphenate = v != 0; }},
    {"footer", 3, [](const Settings& s) { return static_cast<int>(s.footer); },
     [](Settings& s, int v) { s.footer = static_cast<FooterInfo>(v); }},
    {"night", 2, [](const Settings& s) { return s.night ? 1 : 0; }, [](Settings& s, int v) { s.night = v != 0; }},
    {"focus", 2, [](const Settings& s) { return s.focus_reading ? 1 : 0; },
     [](Settings& s, int v) { s.focus_reading = v != 0; }},
    {"book_styles", 2, [](const Settings& s) { return s.publisher_styles ? 1 : 0; },
     [](Settings& s, int v) { s.publisher_styles = v != 0; }},
    {"indent", 3, [](const Settings& s) { return int{s.paragraph_indent}; },
     [](Settings& s, int v) { s.paragraph_indent = static_cast<uint8_t>(v); }},
};

}  // namespace

uint32_t sleep_after_ms(SleepAfter s) {
  switch (s) {
    case SleepAfter::Min5:
      return 5 * 60 * 1000;
    case SleepAfter::Min10:
      return 10 * 60 * 1000;
    case SleepAfter::Min30:
      return 30 * 60 * 1000;
    case SleepAfter::Never:
      break;
  }
  return 0;
}

void load_settings(Storage& storage, Settings& out) {
  char buf[1024];
  const int32_t n = textfile::read(storage, kSettingsPath, buf, sizeof(buf));
  if (n <= 0) return;
  std::string_view text(buf, static_cast<size_t>(n));
  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, eq);
    std::string_view value = line.substr(eq + 1);
    if (!value.empty() && value.back() == '\r') value.remove_suffix(1);
    if (key == "book_font" && !value.empty()) textfile::copy(value, out.book_font, sizeof(out.book_font));
    if (key == "menu_font" && !value.empty()) textfile::copy(value, out.menu_font, sizeof(out.menu_font));
    for (const SettingKey& k : kKeys) {
      const uint32_t v = textfile::to_uint(value, UINT32_MAX);
      if (key == k.name && v < static_cast<uint32_t>(k.count)) k.set(out, static_cast<int>(v));
    }
  }
}

bool save_settings(Storage& storage, const Settings& s) {
  char buf[512];
  int n = 0;
  for (const SettingKey& k : kKeys) {
    n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), "%s=%d\n", k.name, k.get(s));
  }
  n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), "book_font=%s\nmenu_font=%s\n", s.book_font,
                     s.menu_font);
  if (n >= static_cast<int>(sizeof(buf))) return false;
  return storage.write(kSettingsPath, buf, static_cast<uint32_t>(n));
}

}  // namespace vellum

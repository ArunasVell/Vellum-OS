#include "vellum/updates.h"

#include <cstdio>
#include <cstring>
#include <string_view>

#include "vellum/textfile.h"
#include "vellum/upload_page.h"

namespace vellum {

namespace {

// "0.1.42" or "v0.1.42" into up to 4 numbers; false when it is not one.
bool parse_version(const char* s, uint32_t (&parts)[4], int& count) {
  if (*s == 'v' || *s == 'V') ++s;
  count = 0;
  while (count < 4) {
    if (*s < '0' || *s > '9') return false;
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
      if (n > 100000000) return false;
      n = n * 10 + static_cast<uint32_t>(*s++ - '0');
    }
    parts[count++] = n;
    if (*s == '\0') return true;
    if (*s++ != '.') return false;
  }
  return false;
}

// The JSON string after `"key":`, unescaped into out. Only the escapes a
// tag or address can hold are handled; others fail.
bool string_after(std::string_view json, std::string_view key, size_t from, char* out, size_t size, size_t* at) {
  char quoted[48];
  std::snprintf(quoted, sizeof(quoted), "\"%.*s\"", static_cast<int>(key.size()), key.data());
  size_t pos = json.find(quoted, from);
  if (pos == std::string_view::npos) return false;
  pos += std::strlen(quoted);
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\n' || json[pos] == '\t')) ++pos;
  if (pos >= json.size() || json[pos] != '"') return false;
  size_t len = 0;
  for (++pos; pos < json.size() && json[pos] != '"'; ++pos) {
    char c = json[pos];
    if (c == '\\') {
      if (++pos >= json.size()) return false;
      c = json[pos];
      if (c != '/' && c != '"' && c != '\\') return false;
    }
    if (len + 1 >= size) return false;
    out[len++] = c;
  }
  if (pos >= json.size()) return false;
  out[len] = '\0';
  if (at != nullptr) *at = pos;
  return true;
}

void format_stamp(const DateTime& t, char* out, size_t size) {
  std::snprintf(out, size, "%04u-%02u-%02u %02u:%02u:%02u", unsigned{t.year}, unsigned{t.month}, unsigned{t.day},
                unsigned{t.hour}, unsigned{t.minute}, unsigned{t.second});
}

}  // namespace

bool is_newer_version(const char* latest, const char* current) {
  uint32_t a[4] = {};
  uint32_t b[4] = {};
  int na = 0;
  int nb = 0;
  if (latest == nullptr || !parse_version(latest, a, na)) return false;
  if (current == nullptr || !parse_version(current, b, nb)) return true;
  for (int i = 0; i < 4; ++i) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

bool parse_release(const char* json, size_t len, char* version, size_t version_size, char* url, size_t url_size) {
  const std::string_view text(json, len);
  char tag[40];
  if (!string_after(text, "tag_name", 0, tag, sizeof(tag), nullptr)) return false;
  const char* v = tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag;
  if (std::strlen(v) + 1 > version_size) return false;
  std::snprintf(version, version_size, "%s", v);
  // The assets: the first whose address ends in ".bin" is the update.
  size_t at = 0;
  char candidate[512];
  while (string_after(text, "browser_download_url", at, candidate, sizeof(candidate), &at)) {
    const size_t n = std::strlen(candidate);
    if (n > 4 && std::strcmp(candidate + n - 4, ".bin") == 0 && std::strncmp(candidate, "https://", 8) == 0) {
      if (n + 1 > url_size) return false;
      std::snprintf(url, url_size, "%s", candidate);
      return true;
    }
  }
  return false;
}

uint32_t seconds_until_hour(const DateTime& now, int hour) {
  const int32_t now_s = now.hour * 3600 + now.minute * 60 + now.second;
  int32_t wait = hour * 3600 - now_s;
  if (wait <= 0) wait += 24 * 3600;
  return static_cast<uint32_t>(wait);
}

void load_update_state(Storage& storage, UpdateState& out) {
  out = {};
  char buf[256];
  const int32_t n = textfile::read(storage, kUpdateStatePath, buf, sizeof(buf));
  if (n <= 0) return;
  std::string_view text(buf, static_cast<size_t>(n));
  while (!text.empty()) {
    const size_t end = text.find('\n');
    const std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, eq);
    const std::string_view value = line.substr(eq + 1);
    if (key == "latest") {
      textfile::copy(value, out.latest, sizeof(out.latest));
    } else if (key == "checked") {
      out.has_checked = parse_datetime(value.data(), value.size(), out.checked);
    } else if (key == "night_restart") {
      out.night_restart = value == "1";
    }
  }
}

bool save_update_state(Storage& storage, const UpdateState& s) {
  char stamp[32] = "";
  if (s.has_checked) format_stamp(s.checked, stamp, sizeof(stamp));
  char buf[160];
  const int n = std::snprintf(buf, sizeof(buf), "latest=%s\nchecked=%s\nnight_restart=%d\n", s.latest, stamp,
                              s.night_restart ? 1 : 0);
  return n > 0 && storage.write(kUpdateStatePath, buf, static_cast<uint32_t>(n));
}

}  // namespace vellum

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

bool release_body(const char* json, size_t len, char* out, size_t size) {
  if (size == 0) return false;
  const std::string_view text(json, len);
  size_t pos = text.find("\"body\"");
  if (pos == std::string_view::npos) return false;
  pos += 6;
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == ':' || text[pos] == '\n' || text[pos] == '\t')) ++pos;
  if (pos >= text.size() || text[pos] != '"') return false;
  size_t n = 0;
  size_t line_end = 0;  // where the last whole line ends in out
  const auto put = [&](const char* bytes, size_t count) {
    if (n + count + 1 > size) return false;
    std::memcpy(out + n, bytes, count);
    n += count;
    if (bytes[count - 1] == '\n') line_end = n;
    return true;
  };
  // Four hex digits at text[at], or -1.
  const auto hex4 = [&text](size_t at) {
    if (at + 4 > text.size()) return -1L;
    long v = 0;
    for (size_t i = at; i < at + 4; ++i) {
      const char c = text[i];
      const int d = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
      if (d < 0) return -1L;
      v = v * 16 + d;
    }
    return v;
  };
  bool full = false;
  for (++pos; pos < text.size() && text[pos] != '"' && !full; ++pos) {
    char c = text[pos];
    if (c != '\\') {
      full = !put(&c, 1);
      continue;
    }
    if (++pos >= text.size()) break;
    c = text[pos];
    switch (c) {
      case 'n':
        full = !put("\n", 1);
        break;
      case 'r':
        break;  // the lines end with \n alone
      case 't':
        full = !put(" ", 1);
        break;
      case 'u': {
        long cp = hex4(pos + 1);
        if (cp < 0) return false;
        pos += 4;
        if (cp >= 0xD800 && cp < 0xDC00 && pos + 6 < text.size() && text[pos + 1] == '\\' && text[pos + 2] == 'u') {
          const long low = hex4(pos + 3);
          if (low >= 0xDC00 && low < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            pos += 6;
          }
        }
        char utf8[4];
        size_t k = 0;
        if (cp < 0x80) {
          utf8[k++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
          utf8[k++] = static_cast<char>(0xC0 | cp >> 6);
          utf8[k++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
          utf8[k++] = static_cast<char>(0xE0 | cp >> 12);
          utf8[k++] = static_cast<char>(0x80 | (cp >> 6 & 0x3F));
          utf8[k++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
          utf8[k++] = static_cast<char>(0xF0 | cp >> 18);
          utf8[k++] = static_cast<char>(0x80 | (cp >> 12 & 0x3F));
          utf8[k++] = static_cast<char>(0x80 | (cp >> 6 & 0x3F));
          utf8[k++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        full = !put(utf8, k);
        break;
      }
      default:
        full = !put(&c, 1);  // \" \\ \/ and the rest as themselves
        break;
    }
  }
  if (full) n = line_end;  // no half line, nor half a letter
  out[n] = '\0';
  return true;
}

size_t tidy_release_notes(const char* md, size_t len, char* out, size_t size) {
  if (size == 0) return 0;
  std::string_view text(md, len);
  const size_t news = text.find("## New in this version");
  if (news != std::string_view::npos && (news == 0 || text[news - 1] == '\n')) {
    const size_t next = text.find('\n', news);
    text = next == std::string_view::npos ? std::string_view{} : text.substr(next + 1);
  }
  // Each entry is no longer than its line, so writing over `md` is safe.
  size_t n = 0;
  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.remove_suffix(1);
    bool heading = false;
    if (line.substr(0, 4) == "### ") {
      line.remove_prefix(4);
      heading = true;
    } else if (line.substr(0, 2) == "* " || line.substr(0, 2) == "- ") {
      line.remove_prefix(2);
      // Without the pull request's number at the end.
      const size_t open = line.rfind(" (#");
      if (open != std::string_view::npos && line.back() == ')') {
        bool digits = open + 4 < line.size();
        for (size_t i = open + 3; i + 1 < line.size(); ++i) digits = digits && line[i] >= '0' && line[i] <= '9';
        if (digits) line = line.substr(0, open);
      }
    } else {
      continue;
    }
    while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
    if (line.empty()) continue;
    const size_t need = line.size() + (heading ? 1 : 0) + 1;
    if (n + need + 1 > size) break;
    if (heading) out[n++] = '#';
    std::memmove(out + n, line.data(), line.size());
    n += line.size();
    out[n++] = '\n';
  }
  out[n] = '\0';
  return n;
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
    } else if (key == "started") {
      textfile::copy(value, out.started, sizeof(out.started));
    }
  }
}

bool save_update_state(Storage& storage, const UpdateState& s) {
  char stamp[32] = "";
  if (s.has_checked) format_stamp(s.checked, stamp, sizeof(stamp));
  char buf[192];
  const int n = std::snprintf(buf, sizeof(buf), "latest=%s\nchecked=%s\nnight_restart=%d\nstarted=%s\n", s.latest,
                              stamp, s.night_restart ? 1 : 0, s.started);
  return n > 0 && storage.write(kUpdateStatePath, buf, static_cast<uint32_t>(n));
}

}  // namespace vellum

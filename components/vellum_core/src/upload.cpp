#include <cstdio>
#include <cstring>

#include "vellum/upload_page.h"

namespace vellum {
namespace {

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// True when s (len bytes) ends in `ext` (lowercase, with its dot), in any case.
bool ends_with(const char* s, size_t len, const char* ext) {
  const size_t n = std::strlen(ext);
  if (len < n) return false;
  const char* tail = s + len - n;
  for (size_t i = 0; i < n; ++i) {
    const char c = tail[i] >= 'A' && tail[i] <= 'Z' ? static_cast<char>(tail[i] - 'A' + 'a') : tail[i];
    if (c != ext[i]) return false;
  }
  return true;
}

// The extension in `exts` (a nullptr-terminated list) that s ends in, or nullptr.
const char* matching_ext(const char* s, size_t len, const char* const* exts) {
  for (; *exts != nullptr; ++exts) {
    if (ends_with(s, len, *exts)) return *exts;
  }
  return nullptr;
}

// A file name a browser sent, made safe to save: no folders, no characters
// FAT rejects, no leading dots, at most 120 bytes, ending in one of `exts`.
bool safe_name(const char* in, char* out, size_t size, const char* const* exts) {
  // Keep only the last path part: browsers may send "C:\fakepath\x.epub".
  const char* base = in;
  for (const char* p = in; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  while (*base == '.' || *base == ' ') ++base;  // no hidden files

  constexpr size_t kMax = 120;
  const size_t limit = size - 1 < kMax ? size - 1 : kMax;
  size_t n = 0;
  for (const char* p = base; *p != '\0' && n < limit; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool bad = c < 0x20 || c == 0x7F || std::strchr("<>:\"|?*", c) != nullptr;
    out[n++] = bad ? '_' : static_cast<char>(c);
  }
  // A long name cut mid-way would lose its extension or split a UTF-8
  // character: shorten it and put the extension back.
  if (*base != '\0' && std::strlen(base) > n) {
    const char* ext = matching_ext(base, std::strlen(base), exts);
    const size_t ext_len = ext != nullptr ? std::strlen(ext) : 0;
    if (ext == nullptr || limit < ext_len + 1) return false;
    n = limit - ext_len;
    while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;  // UTF-8 boundary
    std::memcpy(out + n, ext, ext_len);
    n += ext_len;
  }
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) --n;  // FAT drops these
  out[n] = '\0';
  const char* ext = matching_ext(out, n, exts);
  return ext != nullptr && n > std::strlen(ext);
}

constexpr const char* kBookExts[] = {".epub", ".fb2.zip", ".fb2",      ".txt", ".html", ".htm",
                                     ".mobi", ".azw3",    ".azw",      ".prc", ".pdb",  ".pdf",
                                     ".docx", ".md",      ".markdown", ".rtf", ".cbz",  nullptr};
constexpr const char* kFontExts[] = {".ttf", ".otf", nullptr};
constexpr const char* kDictionaryExts[] = {".ifo", ".idx", ".dict", ".dict.dz", ".syn", nullptr};
constexpr const char* kPictureExts[] = {".jpg", ".jpeg", ".png", ".bmp", nullptr};

}  // namespace

bool url_decode(const char* in, size_t in_len, char* out, size_t size) {
  size_t n = 0;
  for (size_t i = 0; i < in_len; ++i) {
    char c = in[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (i + 2 >= in_len) return false;
      const int hi = hex_value(in[i + 1]);
      const int lo = hex_value(in[i + 2]);
      if (hi < 0 || lo < 0) return false;
      c = static_cast<char>(hi * 16 + lo);
      i += 2;
    }
    if (n + 1 >= size) return false;
    out[n++] = c;
  }
  if (size == 0) return false;
  out[n] = '\0';
  return true;
}

bool form_field(const char* body, size_t len, const char* key, char* out, size_t size) {
  const size_t key_len = std::strlen(key);
  size_t start = 0;
  while (start <= len) {
    size_t end = start;
    while (end < len && body[end] != '&') ++end;
    if (end - start > key_len && body[start + key_len] == '=' && std::strncmp(body + start, key, key_len) == 0) {
      return url_decode(body + start + key_len + 1, end - start - key_len - 1, out, size);
    }
    if (end - start == key_len && std::strncmp(body + start, key, key_len) == 0) {
      return url_decode("", 0, out, size);  // "key" alone: empty
    }
    start = end + 1;
  }
  return false;
}

bool safe_book_name(const char* in, char* out, size_t size) { return safe_name(in, out, size, kBookExts); }

bool safe_font_name(const char* in, char* out, size_t size) { return safe_name(in, out, size, kFontExts); }

bool safe_dictionary_name(const char* in, char* out, size_t size) { return safe_name(in, out, size, kDictionaryExts); }

bool safe_picture_name(const char* in, char* out, size_t size) { return safe_name(in, out, size, kPictureExts); }

bool json_escape(const char* s, char* out, size_t size, size_t& len) {
  for (; *s != '\0'; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    char esc[8];
    int n = 1;
    if (c == '"' || c == '\\') {
      esc[0] = '\\';
      esc[1] = static_cast<char>(c);
      n = 2;
    } else if (c < 0x20) {
      n = std::snprintf(esc, sizeof(esc), "\\u%04x", c);
    } else {
      esc[0] = static_cast<char>(c);
    }
    if (len + n + 1 > size) return false;
    std::memcpy(out + len, esc, n);
    len += n;
  }
  out[len] = '\0';
  return true;
}

bool parse_datetime(const char* in, size_t len, DateTime& out) {
  // Digits at these offsets, separators between them.
  static constexpr char kShape[] = "dddd-dd-dd dd:dd:dd";
  if (len != sizeof(kShape) - 1) return false;
  for (size_t i = 0; i < len; ++i) {
    const bool digit = in[i] >= '0' && in[i] <= '9';
    if (kShape[i] == 'd' ? !digit : in[i] != kShape[i]) return false;
  }
  auto num = [&](size_t at, size_t n) {
    int v = 0;
    for (size_t i = at; i < at + n; ++i) v = v * 10 + (in[i] - '0');
    return v;
  };
  const int year = num(0, 4), month = num(5, 2), day = num(8, 2);
  const int hour = num(11, 2), minute = num(14, 2), second = num(17, 2);
  static constexpr int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > kDays[month - 1]) return false;
  if (month == 2 && day == 29 && year % 4 != 0) return false;  // 2000 is a leap year; 2100 is out of range
  if (hour > 23 || minute > 59 || second > 59) return false;
  out = {static_cast<uint16_t>(year), static_cast<uint8_t>(month),  static_cast<uint8_t>(day),
         static_cast<uint8_t>(hour),  static_cast<uint8_t>(minute), static_cast<uint8_t>(second)};
  return true;
}

}  // namespace vellum

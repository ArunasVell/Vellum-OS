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

bool ends_with_epub(const char* s, size_t len) {
  if (len < 5) return false;
  const char* ext = s + len - 5;
  const char* want = ".epub";
  for (int i = 0; i < 5; ++i) {
    const char c = ext[i] >= 'A' && ext[i] <= 'Z' ? static_cast<char>(ext[i] - 'A' + 'a') : ext[i];
    if (c != want[i]) return false;
  }
  return true;
}

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

bool safe_book_name(const char* in, char* out, size_t size) {
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
  // A long name cut mid-way would lose its ".epub" or split a UTF-8
  // character: shorten it and put the extension back.
  if (*base != '\0' && std::strlen(base) > n) {
    if (!ends_with_epub(base, std::strlen(base)) || limit < 6) return false;
    n = limit - 5;
    while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;  // UTF-8 boundary
    std::memcpy(out + n, ".epub", 5);
    n += 5;
  }
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) --n;  // FAT drops these
  out[n] = '\0';
  return n > 5 && ends_with_epub(out, n);
}

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

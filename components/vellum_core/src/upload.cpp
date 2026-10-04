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

}  // namespace vellum

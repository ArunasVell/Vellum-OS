// Small text files on the card: read whole, split into lines and
// tab-separated fields. Used for settings, progress and the library cache.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vellum/hal.h"

namespace vellum::textfile {

// Reads the whole file into buf (NUL-terminated). Returns its length, or -1
// when it is missing, unreadable or does not fit.
int32_t read(Storage& storage, const char* path, char* buf, uint32_t cap);

// Calls fn(fields, count) for each non-empty line, split on tabs (at most
// kMaxFields fields).
constexpr int kMaxFields = 8;
template <typename Fn>
void for_each_line(std::string_view text, Fn&& fn) {
  while (!text.empty()) {
    size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) continue;
    std::string_view fields[kMaxFields];
    int n = 0;
    while (n < kMaxFields) {
      const size_t tab = line.find('\t');
      fields[n++] = line.substr(0, tab);
      if (tab == std::string_view::npos) break;
      line.remove_prefix(tab + 1);
    }
    fn(fields, n);
  }
}

// Parses a decimal number; returns `fallback` when s is not one.
uint32_t to_uint(std::string_view s, uint32_t fallback = 0);

// Copies s into out (NUL-terminated, cut to fit).
void copy(std::string_view s, char* out, size_t size);

}  // namespace vellum::textfile

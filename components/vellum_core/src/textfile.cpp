#include "vellum/textfile.h"

#include <cstring>

namespace vellum::textfile {

int32_t read(Storage& storage, const char* path, char* buf, uint32_t cap) {
  File* f = storage.open(path);
  if (f == nullptr) return -1;
  const uint32_t size = f->size();
  int32_t n = -1;
  if (size < cap) n = f->read_at(0, buf, size);
  storage.close(f);
  if (n != static_cast<int32_t>(size)) return -1;
  buf[n] = '\0';
  return n;
}

uint32_t to_uint(std::string_view s, uint32_t fallback) {
  if (s.empty()) return fallback;
  uint32_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return fallback;
    v = v * 10 + static_cast<uint32_t>(c - '0');
  }
  return v;
}

void copy(std::string_view s, char* out, size_t size) {
  const size_t n = s.size() < size - 1 ? s.size() : size - 1;
  std::memcpy(out, s.data(), n);
  out[n] = '\0';
}

}  // namespace vellum::textfile

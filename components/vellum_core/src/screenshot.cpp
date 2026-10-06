#include "vellum/screenshot.h"

#include <cstdio>
#include <cstring>

namespace vellum::screenshot {
namespace {

void put16(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  put16(p, v);
  put16(p + 2, v >> 16);
}

}  // namespace

void next_path(Storage& storage, char* out, size_t size) {
  int highest = 0;
  storage.list(
      kFolder,
      [](void* ctx, const char* name, bool is_dir, uint32_t) {
        int n = 0;
        if (!is_dir && std::sscanf(name, "screenshot-%d.bmp", &n) == 1 && n > *static_cast<int*>(ctx)) {
          *static_cast<int*>(ctx) = n;
        }
      },
      &highest);
  std::snprintf(out, size, "%s/screenshot-%04d.bmp", kFolder, highest + 1);
}

bool save_bmp(Storage& storage, const Framebuffer& fb, const char* path) {
  const int w = fb.width();
  const int h = fb.height();
  const uint32_t stride = static_cast<uint32_t>((w + 31) / 32 * 4);
  // File header, info header, then the colours: 0 black, 1 white, as the
  // framebuffer stores them.
  uint8_t head[62] = {'B', 'M'};
  put32(head + 2, bmp_size(w, h));
  put32(head + 10, sizeof(head));
  put32(head + 14, 40);
  put32(head + 18, static_cast<uint32_t>(w));
  put32(head + 22, static_cast<uint32_t>(h));  // positive: rows run bottom to top
  put16(head + 26, 1);                         // planes
  put16(head + 28, 1);                         // bits per pixel
  put32(head + 34, stride * static_cast<uint32_t>(h));
  put32(head + 38, 2835);  // 72 dpi, in pixels per metre
  put32(head + 42, 2835);
  put32(head + 46, 2);
  std::memset(head + 58, 0xFF, 3);  // white

  Writer* out = storage.create(path);
  if (out == nullptr) return false;
  bool ok = out->write(head, sizeof(head));
  uint8_t row[128];  // 1024 px wide at most
  if (stride > sizeof(row)) ok = false;
  for (int y = h - 1; y >= 0 && ok; --y) {
    std::memset(row, 0, stride);
    for (int x = 0; x < w; ++x) {
      if (fb.pixel(x, y) == Color::White) row[x >> 3] |= static_cast<uint8_t>(0x80 >> (x & 7));
    }
    ok = out->write(row, stride);
  }
  return storage.finish(out, ok) && ok;
}

}  // namespace vellum::screenshot

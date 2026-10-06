#include "vellum/bmp.h"

#include <cstring>
#include <new>

namespace vellum::bmp {
namespace {

uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
int get16(const uint8_t* p) { return p[0] | (p[1] << 8); }

uint8_t grey(uint8_t b, uint8_t g, uint8_t r) { return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8); }

}  // namespace

bool read(File& file, const Sink& sink) {
  // The file header (14 bytes) and the start of the info header.
  uint8_t head[54];
  if (file.read_at(0, head, sizeof(head)) != static_cast<int32_t>(sizeof(head)) || head[0] != 'B' || head[1] != 'M') {
    return false;
  }
  const uint32_t data_at = get32(head + 10);
  const uint32_t info_size = get32(head + 14);
  if (info_size < 40) return false;  // the old OS/2 header is not read
  const int w = static_cast<int32_t>(get32(head + 18));
  const int32_t h_signed = static_cast<int32_t>(get32(head + 22));
  const bool top_down = h_signed < 0;
  const int h = top_down ? -h_signed : h_signed;
  const int bits = get16(head + 28);
  const uint32_t compression = get32(head + 30);
  // Uncompressed, or 32-bit with the usual colour masks.
  if (compression != 0 && !(compression == 3 && bits == 32)) return false;
  if (w <= 0 || h <= 0 || w > kMaxWidth || (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)) {
    return false;
  }

  // The palette as grey, for 1, 4 and 8 bits.
  uint8_t palette[256];
  if (bits <= 8) {
    uint32_t colours = get32(head + 46);
    if (colours == 0 || colours > (1u << bits)) colours = 1u << bits;
    uint8_t raw[256 * 4];
    if (file.read_at(14 + info_size, raw, colours * 4) != static_cast<int32_t>(colours * 4)) return false;
    std::memset(palette, 0, sizeof(palette));
    for (uint32_t i = 0; i < colours; ++i) palette[i] = grey(raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2]);
  }

  // One row of the file and its grey, allocated on first use.
  static uint8_t* row = new (std::nothrow) uint8_t[kMaxWidth * 4 + kMaxWidth];
  if (row == nullptr || !sink.size(sink.ctx, w, h)) return false;
  uint8_t* out = row + kMaxWidth * 4;
  const uint32_t stride = ((static_cast<uint32_t>(w) * bits + 31) / 32) * 4;
  for (int i = 0; i < h; ++i) {
    if (file.read_at(data_at + static_cast<uint32_t>(i) * stride, row, stride) != static_cast<int32_t>(stride)) {
      return false;
    }
    for (int x = 0; x < w; ++x) {
      switch (bits) {
        case 1:
          out[x] = palette[(row[x >> 3] >> (7 - (x & 7))) & 1];
          break;
        case 4:
          out[x] = palette[(row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0x0F];
          break;
        case 8:
          out[x] = palette[row[x]];
          break;
        default: {
          const uint8_t* p = row + x * (bits / 8);  // blue, green, red
          out[x] = grey(p[0], p[1], p[2]);
        }
      }
    }
    sink.row(sink.ctx, top_down ? i : h - 1 - i, out);
  }
  return true;
}

}  // namespace vellum::bmp

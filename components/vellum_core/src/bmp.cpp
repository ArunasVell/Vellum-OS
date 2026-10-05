#include "vellum/bmp.h"

namespace vellum::bmp {
namespace {

uint32_t u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

uint8_t grey(uint32_t r, uint32_t g, uint32_t b) { return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8); }

// A bit field mask as its lowest bit and width; only its top 8 bits are kept.
bool split_mask(uint32_t mask, uint8_t& shift, uint8_t& bits) {
  if (mask == 0) return false;
  int s = 0;
  while (!(mask & (1u << s))) ++s;
  int n = 0;
  while (s + n < 32 && (mask & (1u << (s + n)))) ++n;
  if (n > 8) {
    s += n - 8;
    n = 8;
  }
  shift = static_cast<uint8_t>(s);
  bits = static_cast<uint8_t>(n);
  return true;
}

// A field of `bits` bits widened to 0..255.
uint32_t widen(uint32_t v, int bits) { return bits >= 8 ? v : v * 255 / ((1u << bits) - 1); }

}  // namespace

bool read_info(File& f, Info& out) {
  uint8_t head[66] = {};
  const int32_t n = f.read_at(0, head, sizeof(head));
  if (n < 26 || head[0] != 'B' || head[1] != 'M') return false;
  out = {};
  out.data_at = u32(head + 10);
  const uint32_t dib = u32(head + 14);
  uint32_t compression = 0, colours = 0;
  int entry = 4;    // bytes per palette colour
  if (dib == 12) {  // the old OS/2 header: 16-bit sides, 3-byte colours
    out.w = static_cast<int>(u16(head + 18));
    out.h = static_cast<int>(u16(head + 20));
    out.bpp = static_cast<int>(u16(head + 24));
    entry = 3;
  } else if (dib >= 40 && n >= 54) {
    const auto h = static_cast<int32_t>(u32(head + 22));
    out.w = static_cast<int32_t>(u32(head + 18));
    out.top_down = h < 0;
    out.h = h < 0 ? -h : h;
    out.bpp = static_cast<int>(u16(head + 28));
    compression = u32(head + 30);
    colours = u32(head + 46);
  } else {
    return false;
  }
  if (out.w <= 0 || out.h <= 0 || out.w > kMaxSide || out.h > kMaxSide) return false;
  const bool fields = compression == 3 && out.bpp == 32;  // BI_BITFIELDS
  if (compression != 0 && !fields) return false;
  if (out.bpp != 1 && out.bpp != 4 && out.bpp != 8 && out.bpp != 24 && out.bpp != 32) return false;
  out.row_bytes = static_cast<uint32_t>((static_cast<uint64_t>(out.w) * out.bpp + 31) / 32 * 4);
  if (out.data_at < 14 + dib || out.data_at >= f.size()) return false;

  if (fields) {
    // The masks follow a 40-byte header, or sit inside a larger one: at 54 either way.
    if (n < 66) return false;
    for (int c = 0; c < 3; ++c) {
      if (!split_mask(u32(head + 54 + 4 * c), out.shift[c], out.bits[c])) return false;
    }
  }
  if (out.bpp <= 8) {
    const uint32_t max = 1u << out.bpp;
    if (colours == 0 || colours > max) colours = max;
    uint8_t pal[256 * 4];
    const uint32_t bytes = colours * static_cast<uint32_t>(entry);
    if (f.read_at(14 + dib, pal, bytes) != static_cast<int32_t>(bytes)) return false;
    for (uint32_t i = 0; i < colours; ++i) {
      const uint8_t* p = pal + i * entry;  // blue, green, red
      out.palette[i] = grey(p[2], p[1], p[0]);
    }
  }
  return true;
}

bool read_row(File& f, const Info& info, int y, uint8_t* raw, uint8_t* out) {
  if (y < 0 || y >= info.h) return false;
  const int stored = info.top_down ? y : info.h - 1 - y;
  const uint64_t at = info.data_at + static_cast<uint64_t>(stored) * info.row_bytes;
  if (at + info.row_bytes > f.size()) return false;
  if (f.read_at(static_cast<uint32_t>(at), raw, info.row_bytes) != static_cast<int32_t>(info.row_bytes)) return false;
  switch (info.bpp) {
    case 1:
    case 4:
    case 8: {
      const int per_byte = 8 / info.bpp;
      const int mask = (1 << info.bpp) - 1;
      for (int x = 0; x < info.w; ++x) {
        const int shift = 8 - info.bpp * (x % per_byte + 1);  // the leftmost pixel in the high bits
        out[x] = info.palette[(raw[x / per_byte] >> shift) & mask];
      }
      break;
    }
    case 24:
      for (int x = 0; x < info.w; ++x) out[x] = grey(raw[3 * x + 2], raw[3 * x + 1], raw[3 * x]);
      break;
    case 32:
      for (int x = 0; x < info.w; ++x) {
        const uint32_t v = u32(raw + 4 * x);
        uint32_t c[3];
        for (int k = 0; k < 3; ++k) c[k] = widen((v >> info.shift[k]) & ((1u << info.bits[k]) - 1), info.bits[k]);
        out[x] = grey(c[0], c[1], c[2]);
      }
      break;
    default:
      return false;
  }
  return true;
}

}  // namespace vellum::bmp

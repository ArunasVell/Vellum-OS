#include "png_writer.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace sim {
namespace {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void put_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body) {
  put_u32(out, static_cast<uint32_t>(body.size()));
  std::vector<uint8_t> typed(type, type + 4);
  typed.insert(typed.end(), body.begin(), body.end());
  out.insert(out.end(), typed.begin(), typed.end());
  put_u32(out, crc32(typed.data(), typed.size()));
}

}  // namespace

bool write_gray_png(const std::string& path, int width, int height, const std::vector<uint8_t>& pixels) {
  // Raw scanlines, each prefixed with filter type 0.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (width + 1));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), pixels.begin() + static_cast<long>(y) * width,
               pixels.begin() + static_cast<long>(y + 1) * width);
  }

  // zlib stream made of stored (uncompressed) deflate blocks.
  std::vector<uint8_t> z{0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  size_t pos = 0;
  do {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool final = pos + n == raw.size();
    z.push_back(final ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n));
    z.push_back(static_cast<uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
    pos += n;
  } while (pos < raw.size());
  put_u32(z, (b << 16) | a);

  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put_u32(ihdr, static_cast<uint32_t>(width));
  put_u32(ihdr, static_cast<uint32_t>(height));
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});  // 8-bit, grayscale, deflate, no filter, no interlace
  put_chunk(png, "IHDR", ihdr);
  put_chunk(png, "IDAT", z);
  put_chunk(png, "IEND", {});

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace sim

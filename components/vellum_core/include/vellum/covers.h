// Book cover pictures: decoded once from the EPUB (JPEG or PNG), shrunk to
// two grey thumbnails and kept on the card, so screens only read a few
// kilobytes and never decode while drawing.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

// A grey picture: 0 is black, 255 white, rows top to bottom.
struct CoverImage {
  int w = 0;
  int h = 0;
  uint8_t* px = nullptr;

  bool valid() const { return px != nullptr && w > 0 && h > 0; }
};

namespace covers {

// The large thumbnail fills the sleep screen (480 x 800, cut to its shape)
// with little enlarging; the small one fits two list-row covers, for
// sharper shrinking.
constexpr int kLargeW = 400;
constexpr int kLargeH = 640;
constexpr int kSmallW = 84;
constexpr int kSmallH = 120;
constexpr const char* kFolder = "vellum/covers";

// Where the thumbnails of the book at `book_path` (`file_size` bytes) live.
void path_for(const char* book_path, uint32_t file_size, char* out, size_t size);

// Decodes a JPEG (baseline) or PNG cover and writes its thumbnails to
// `cover_path`. Returns false when the picture cannot be decoded.
bool make(Storage& storage, const char* cover_path, const uint8_t* data, uint32_t len);

// Reads one thumbnail into out.px, which must hold kLargeW*kLargeH (large)
// or kSmallW*kSmallH (small) bytes. Returns false when there is none.
bool load(Storage& storage, const char* cover_path, bool large, CoverImage& out);

}  // namespace covers
}  // namespace vellum

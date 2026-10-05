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

// Full-screen pictures, for sleep screens: cut to the screen's shape and
// shrunk to at most kScreenW x kScreenH (never enlarged), with a thumbnail
// for choosing among them. Converting and loading use the same work
// buffers as the book covers, so they cost no memory of their own.
constexpr int kScreenW = 480;
constexpr int kScreenH = 800;
constexpr int kThumbW = 120;
constexpr int kThumbH = 200;
constexpr uint32_t kMaxPngFile = 512 * 1024;  // a PNG is read whole; larger ones are refused

// Decodes the picture file at `picture_path` (JPEG, PNG or uncompressed
// BMP, told apart by their first bytes) and writes the screen picture and
// its thumbnail to `out_path`. Returns false when it cannot be decoded.
bool make_screen(Storage& storage, const char* picture_path, const char* out_path);

// Reads a screen picture into memory kept here, valid until the next call
// into covers. nullptr when there is none (or it is broken).
const CoverImage* load_screen(Storage& storage, const char* path);

// Reads a screen picture's thumbnail into out.px, which must hold
// kThumbW*kThumbH bytes. Returns false when there is none.
bool load_thumb(Storage& storage, const char* path, CoverImage& out);

}  // namespace covers
}  // namespace vellum

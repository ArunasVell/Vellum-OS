// BMP pictures (Windows bitmaps), for sleep screens and the picture viewer:
// uncompressed 1, 4, 8, 24 and 32 bits per pixel, read from the card a row at a time as grey, so
// a large picture never has to fit in memory.
#pragma once

#include <cstdint>

#include "vellum/hal.h"

namespace vellum::bmp {

constexpr int kMaxSide = 16384;

struct Info {
  int w = 0;
  int h = 0;
  int bpp = 0;
  bool top_down = false;   // rows stored from the top (most BMPs start at the bottom)
  uint32_t data_at = 0;    // file offset of the first stored row
  uint32_t row_bytes = 0;  // one stored row, with its padding to 4 bytes
  // 32 bits with bit fields: where red, green and blue are (lowest bit and width).
  uint8_t shift[3] = {16, 8, 0};
  uint8_t bits[3] = {8, 8, 8};
  uint8_t palette[256] = {};  // the grey of each colour, for 1, 4 and 8 bits
};

// Reads the headers (and palette). Returns false for anything this reader
// does not handle: compressed pictures, 16 bits, broken headers.
bool read_info(File& f, Info& out);

// Reads row y (0 = top) as w grey bytes (0 black, 255 white). `raw` must
// hold info.row_bytes.
bool read_row(File& f, const Info& info, int y, uint8_t* raw, uint8_t* grey);

constexpr int kMaxWidth = 4096;

// size(ctx, w, h) is called first and may refuse the picture (false); then
// row(ctx, y, grey) for every row, y counted from the top, in the file's
// order (usually bottom to top). `grey` holds w bytes, 0 black to 255 white.
struct Sink {
  void* ctx = nullptr;
  bool (*size)(void* ctx, int w, int h) = nullptr;
  void (*row)(void* ctx, int y, const uint8_t* grey) = nullptr;
};

// Returns false when the file is not a bitmap of those kinds, is wider than
// kMaxWidth, or cannot be read whole.
bool read(File& file, const Sink& sink);

}  // namespace vellum::bmp

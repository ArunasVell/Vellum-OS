// Windows bitmaps (.bmp) for the picture viewer: uncompressed, 1, 4, 8, 24
// or 32 bits a pixel, read row by row as grey so a large one needs only a
// row's worth of memory.
#pragma once

#include <cstdint>

#include "vellum/hal.h"

namespace vellum::bmp {

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

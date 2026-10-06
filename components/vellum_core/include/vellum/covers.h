// Book cover pictures: decoded once from the EPUB (JPEG or PNG), shrunk to
// two grey thumbnails and kept on the card, so screens only read a few
// kilobytes and never decode while drawing.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/framebuffer.h"
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

// The picture viewer: decodes a JPEG (baseline, any size: read as it
// decodes), PNG (up to 1 MB) or BMP file shrunk to fit kLargeW x kLargeH
// into out.px, which must hold that many bytes. Returns false when the
// file cannot be read or decoded.
bool load_picture(Storage& storage, const char* path, CoverImage& out);
// Pictures inside books use the same decoders and work buffers. Each is
// decoded when its page is first drawn, at the size it is shown, and kept
// on the card already dithered (1 bit a pixel), so showing it again reads
// a few kilobytes and decodes nothing.
constexpr const char* kPictureFolder = "vellum/pictures";

// The size of a picture from its first bytes: 1 when found and it can be
// decoded, 0 when it cannot (another format, a progressive JPEG, a PNG of
// over two million pixels), -1 when more bytes are needed.
int picture_size(const uint8_t* data, uint32_t len, int& w, int& h);

// Where picture `entry` of the book `book_key` is kept when drawn w x h.
void picture_path(uint32_t book_key, int entry, int w, int h, char* out, size_t size);

// Draws the kept picture at `path` into r (its size must match). `scratch`
// holds at least r.w * r.h / 8 + 16 bytes. False when there is none.
bool draw_kept(Storage& storage, const char* path, Framebuffer& fb, Rect r, uint8_t* scratch, uint32_t scratch_size);

// Decodes a JPEG or PNG, draws it dithered into r (scaled to fill it) and,
// with `storage`, keeps it at `path`. `scratch` (as for draw_kept) may be
// the picture's own data. False when it cannot be decoded.
bool draw_new(Storage* storage, const char* path, Framebuffer& fb, Rect r, const uint8_t* data, uint32_t len,
              uint8_t* scratch, uint32_t scratch_size);

}  // namespace covers
}  // namespace vellum

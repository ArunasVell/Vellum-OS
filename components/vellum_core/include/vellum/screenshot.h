// Screenshots: the screen as the reader sees it, saved to the card as a
// 1-bit BMP in screenshots/ (screenshot-0001.bmp, -0002 ...).
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/framebuffer.h"
#include "vellum/hal.h"

namespace vellum::screenshot {

constexpr const char* kFolder = "screenshots";

// Bytes in a 1-bit BMP of w x h pixels: headers, two colours, padded rows.
constexpr uint32_t bmp_size(int w, int h) {
  return 62 + static_cast<uint32_t>((w + 31) / 32 * 4) * static_cast<uint32_t>(h);
}

// The next free name, after the highest number already in the folder.
void next_path(Storage& storage, char* out, size_t size);

// Writes fb's logical (rotated) image to `path`, a row at a time.
bool save_bmp(Storage& storage, const Framebuffer& fb, const char* path);

}  // namespace vellum::screenshot

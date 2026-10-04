// 1-bit framebuffer with rotation and dirty-region tracking.
//
// The buffer is stored in the panel's physical orientation (rows of packed
// bits, MSB = leftmost pixel, 1 = white), so it can be streamed to the
// controller without conversion. Drawing calls take logical coordinates;
// the rotation maps them onto the physical buffer.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vellum {

enum class Color : uint8_t { Black = 0, White = 1 };

enum class Rotation : uint8_t { R0, R90, R180, R270 };

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool empty() const { return w <= 0 || h <= 0; }
  Rect united(const Rect& o) const;
  Rect intersected(const Rect& o) const;
  bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

class Framebuffer {
 public:
  static constexpr size_t bytes_for(int physical_w, int physical_h) {
    return static_cast<size_t>((physical_w + 7) / 8) * static_cast<size_t>(physical_h);
  }

  // `buffer` must hold bytes_for(physical_w, physical_h) bytes and outlive the framebuffer.
  Framebuffer(uint8_t* buffer, int physical_w, int physical_h);

  void set_rotation(Rotation r) { rotation_ = r; }
  Rotation rotation() const { return rotation_; }

  // Logical size, after rotation.
  int width() const;
  int height() const;

  int physical_width() const { return pw_; }
  int physical_height() const { return ph_; }
  size_t stride() const { return stride_; }
  const uint8_t* data() const { return buf_; }

  void clear(Color c);
  void set_pixel(int x, int y, Color c);
  Color pixel(int x, int y) const;

  void fill_rect(Rect r, Color c);
  void draw_rect(Rect r, Color c, int thickness = 1);
  void fill_round_rect(Rect r, int radius, Color c);
  void hline(int x, int y, int w, Color c) { fill_rect({x, y, w, 1}, c); }
  void vline(int x, int y, int h, Color c) { fill_rect({x, y, 1, h}, c); }

  // Changed area since the last clear_dirty(), in physical coordinates.
  Rect dirty() const { return dirty_; }
  void clear_dirty() { dirty_ = {}; }
  void mark_all_dirty() { dirty_ = {0, 0, pw_, ph_}; }

  // Maps a logical rectangle to the physical rectangle it covers.
  Rect to_physical(Rect r) const;

 private:
  void to_physical(int x, int y, int& px, int& py) const;
  void fill_physical(Rect r, Color c);

  uint8_t* buf_;
  int pw_;
  int ph_;
  size_t stride_;
  Rotation rotation_ = Rotation::R0;
  Rect dirty_;
};

}  // namespace vellum

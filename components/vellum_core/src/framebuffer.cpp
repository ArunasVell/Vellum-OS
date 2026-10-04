#include "vellum/framebuffer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vellum {

Rect Rect::united(const Rect& o) const {
  if (empty()) return o;
  if (o.empty()) return *this;
  const int x0 = std::min(x, o.x);
  const int y0 = std::min(y, o.y);
  const int x1 = std::max(x + w, o.x + o.w);
  const int y1 = std::max(y + h, o.y + o.h);
  return {x0, y0, x1 - x0, y1 - y0};
}

Rect Rect::intersected(const Rect& o) const {
  const int x0 = std::max(x, o.x);
  const int y0 = std::max(y, o.y);
  const int x1 = std::min(x + w, o.x + o.w);
  const int y1 = std::min(y + h, o.y + o.h);
  if (x1 <= x0 || y1 <= y0) return {};
  return {x0, y0, x1 - x0, y1 - y0};
}

Framebuffer::Framebuffer(uint8_t* buffer, int physical_w, int physical_h)
    : buf_(buffer), pw_(physical_w), ph_(physical_h), stride_((physical_w + 7) / 8) {}

int Framebuffer::width() const { return (rotation_ == Rotation::R90 || rotation_ == Rotation::R270) ? ph_ : pw_; }

int Framebuffer::height() const { return (rotation_ == Rotation::R90 || rotation_ == Rotation::R270) ? pw_ : ph_; }

Rect Framebuffer::to_physical(Rect r) const {
  if (r.empty()) return {};
  int ax, ay, bx, by;
  to_physical(r.x, r.y, ax, ay);
  to_physical(r.x + r.w - 1, r.y + r.h - 1, bx, by);
  const int x0 = std::min(ax, bx);
  const int y0 = std::min(ay, by);
  return {x0, y0, std::max(ax, bx) - x0 + 1, std::max(ay, by) - y0 + 1};
}

void Framebuffer::clear(Color c) {
  std::memset(buf_, c == Color::White ? 0xFF : 0x00, stride_ * static_cast<size_t>(ph_));
  mark_all_dirty();
}

void Framebuffer::set_pixel(int x, int y, Color c) {
  if (x < 0 || y < 0 || x >= width() || y >= height()) return;
  int px, py;
  to_physical(x, y, px, py);
  uint8_t& byte = buf_[static_cast<size_t>(py) * stride_ + (px >> 3)];
  const uint8_t bit = 0x80 >> (px & 7);
  byte = (c == Color::White) ? (byte | bit) : (byte & ~bit);
  dirty_ = dirty_.united({px, py, 1, 1});
}

Color Framebuffer::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width() || y >= height()) return Color::White;
  int px, py;
  to_physical(x, y, px, py);
  const uint8_t byte = buf_[static_cast<size_t>(py) * stride_ + (px >> 3)];
  return (byte & (0x80 >> (px & 7))) ? Color::White : Color::Black;
}

void Framebuffer::draw_mask(int x, int y, int w, int h, const uint8_t* mask, uint8_t threshold, Color c) {
  const Rect clip = Rect{x, y, w, h}.intersected({0, 0, width(), height()});
  if (clip.empty()) return;
  const bool white = c == Color::White;
  bool drawn = false;
  for (int row = clip.y - y; row < clip.y - y + clip.h; ++row) {
    for (int col = clip.x - x; col < clip.x - x + clip.w; ++col) {
      const size_t i = static_cast<size_t>(row) * w + col;
      const uint8_t level = (mask[i >> 2] >> (6 - 2 * (i & 3))) & 3;
      if (level < threshold) continue;
      int px, py;
      to_physical(x + col, y + row, px, py);
      uint8_t& byte = buf_[static_cast<size_t>(py) * stride_ + (px >> 3)];
      const uint8_t bit = 0x80 >> (px & 7);
      byte = white ? (byte | bit) : (byte & ~bit);
      drawn = true;
    }
  }
  if (drawn) dirty_ = dirty_.united(to_physical(clip));
}

void Framebuffer::fill_rect(Rect r, Color c) {
  r = r.intersected({0, 0, width(), height()});
  if (r.empty()) return;
  fill_physical(to_physical(r), c);
}

// Byte-wise fill of a physical rectangle: whole bytes are memset, only the
// two edge bytes of each row are masked.
void Framebuffer::fill_physical(Rect r, Color c) {
  const int x0 = r.x;
  const int x1 = r.x + r.w - 1;
  const int b0 = x0 >> 3;
  const int b1 = x1 >> 3;
  const uint8_t m0 = 0xFF >> (x0 & 7);
  const uint8_t m1 = static_cast<uint8_t>(0xFF << (7 - (x1 & 7)));
  const bool white = c == Color::White;

  for (int y = r.y; y < r.y + r.h; ++y) {
    uint8_t* row = buf_ + static_cast<size_t>(y) * stride_;
    if (b0 == b1) {
      const uint8_t m = m0 & m1;
      row[b0] = white ? (row[b0] | m) : (row[b0] & ~m);
      continue;
    }
    row[b0] = white ? (row[b0] | m0) : (row[b0] & ~m0);
    if (b1 > b0 + 1) std::memset(row + b0 + 1, white ? 0xFF : 0x00, static_cast<size_t>(b1 - b0 - 1));
    row[b1] = white ? (row[b1] | m1) : (row[b1] & ~m1);
  }
  dirty_ = dirty_.united(r);
}

void Framebuffer::draw_rect(Rect r, Color c, int thickness) {
  thickness = std::min({thickness, r.w / 2 + 1, r.h / 2 + 1});
  fill_rect({r.x, r.y, r.w, thickness}, c);
  fill_rect({r.x, r.y + r.h - thickness, r.w, thickness}, c);
  fill_rect({r.x, r.y + thickness, thickness, r.h - 2 * thickness}, c);
  fill_rect({r.x + r.w - thickness, r.y + thickness, thickness, r.h - 2 * thickness}, c);
}

void Framebuffer::fill_round_rect(Rect r, int radius, Color c) {
  radius = std::max(0, std::min({radius, r.w / 2, r.h / 2}));
  for (int dy = 0; dy < r.h; ++dy) {
    int inset = 0;
    const int edge = std::min(dy, r.h - 1 - dy);
    if (edge < radius) {
      const float d = static_cast<float>(radius) - static_cast<float>(edge) - 0.5f;
      inset = radius - static_cast<int>(std::lround(std::sqrt(static_cast<float>(radius * radius) - d * d)));
    }
    hline(r.x + inset, r.y + dy, r.w - 2 * inset, c);
  }
}

}  // namespace vellum

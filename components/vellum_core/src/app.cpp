// Placeholder reader: pages through a wireframe book so the display queue,
// input and refresh cadence can be exercised before text rendering exists.
#include "vellum/app.h"

#include <algorithm>
#include <cstdint>

#include "vellum/display_queue.h"

namespace vellum {
namespace {

constexpr int kPages = 12;
constexpr int kMargin = 36;
constexpr int kLineHeight = 30;
constexpr int kBarHeight = 6;

// Small deterministic generator so each page has a stable "text" shape.
struct Lcg {
  uint32_t state;
  uint32_t next() {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
  }
  int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
};

void draw_battery(Framebuffer& fb, int right, int y, int percent) {
  if (percent < 0) return;
  const Rect body{right - 26, y, 22, 12};
  fb.draw_rect(body, Color::Black);
  fb.fill_rect({body.x + body.w, y + 3, 3, 6}, Color::Black);
  const int fill = (body.w - 4) * std::clamp(percent, 0, 100) / 100;
  fb.fill_rect({body.x + 2, y + 2, fill, body.h - 4}, Color::Black);
}

void draw_page(Framebuffer& fb, int page, int battery) {
  const int w = fb.width();
  const int h = fb.height();
  fb.clear(Color::White);

  // Header: chapter title placeholder and battery.
  fb.fill_round_rect({kMargin, 30, 120, kBarHeight}, 3, Color::Black);
  draw_battery(fb, w - kMargin, 27, battery);

  // Body: paragraphs of placeholder lines.
  Lcg rng{static_cast<uint32_t>(page) * 7919u + 17u};
  const int text_w = w - 2 * kMargin;
  int y = 92;
  const int bottom = h - 96;
  while (y + kLineHeight < bottom) {
    const int lines = rng.range(3, 7);
    for (int i = 0; i < lines && y + kLineHeight < bottom; ++i) {
      const int indent = (i == 0) ? 24 : 0;
      const bool last = (i == lines - 1);
      const int len = last ? rng.range(text_w / 4, text_w * 3 / 4) : text_w - indent;
      fb.fill_round_rect({kMargin + indent, y, len, kBarHeight}, 3, Color::Black);
      y += kLineHeight;
    }
    y += 14;
  }

  // Footer: dotted track with reading progress.
  const int track_y = h - 44;
  for (int x = kMargin; x < w - kMargin; x += 3) fb.set_pixel(x, track_y, Color::Black);
  const int done = text_w * (page + 1) / kPages;
  fb.fill_round_rect({kMargin, track_y - 1, done, 3}, 1, Color::Black);
  fb.fill_round_rect({kMargin + done - 4, track_y - 4, 9, 9}, 4, Color::Black);
}

}  // namespace

void run(Board& board) {
  Framebuffer fb(board.framebuffer, board.display.width(), board.display.height());
  fb.set_rotation(board.rotation);
  DisplayQueue queue(board.display, fb);

  int page = 0;
  draw_page(fb, page, board.power.battery_percent());
  queue.submit_full();

  for (;;) {
    InputEvent ev;
    if (!board.input.wait(ev, 60 * 1000)) continue;
    if (ev.type == InputEvent::Type::Quit) return;
    if (ev.type != InputEvent::Type::KeyDown) continue;

    switch (ev.key) {
      case Key::Next:
        if (page + 1 >= kPages) continue;
        ++page;
        break;
      case Key::Prev:
        if (page == 0) continue;
        --page;
        break;
      case Key::Home:
        queue.submit_full();
        continue;
      case Key::Power:
        continue;
    }
    draw_page(fb, page, board.power.battery_percent());
    queue.submit();
  }
}

}  // namespace vellum

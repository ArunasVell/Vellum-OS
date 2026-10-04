// Reader: lays out the built-in sample chapter in Literata and pages through it.
#include "vellum/app.h"

#include <algorithm>
#include <cstdio>

#include "vellum/text.h"

namespace vellum {
namespace {

constexpr int kSide = 28;          // left and right margin
constexpr int kHeaderBase = 40;    // baseline of the running header
constexpr int kTextTop = 70;       // top of the text block
constexpr int kTextBottom = 70;    // space below the text block
constexpr int kFooterBase = 28;    // page number baseline, from the bottom edge
constexpr int kTrackRise = 56;     // progress track, from the bottom edge
constexpr int kOpeningDrop = 120;  // extra space above the text on a chapter's first page

const Font& body() { return fonts::literata_26; }

PageStyle page_style(const Framebuffer& fb, int page) {
  const int top = kTextTop + (page == 0 ? kOpeningDrop : 0);
  return {&body(), {kSide, top, fb.width() - 2 * kSide, fb.height() - top - kTextBottom}, body().size};
}

void draw_centered(Framebuffer& fb, const Font& font, int baseline, std::string_view text) {
  draw_text(fb, font, (fb.width() - text_width_px(font, text)) / 2 * 64, baseline, text);
}

void draw_battery(Framebuffer& fb, int right, int baseline, int percent) {
  if (percent < 0) return;
  const Rect body{right - 24, baseline - 11, 20, 11};
  fb.draw_rect(body, Color::Black);
  fb.fill_rect({body.x + body.w, body.y + 3, 2, 5}, Color::Black);
  const int fill = (body.w - 4) * std::clamp(percent, 0, 100) / 100;
  fb.fill_rect({body.x + 2, body.y + 2, fill, body.h - 4}, Color::Black);
}

}  // namespace

App::App(Board& board)
    : board_(board),
      fb_(board.framebuffer, board.display.width(), board.display.height()),
      queue_(board.display, fb_),
      book_(sample_book()) {
  fb_.set_rotation(board.rotation);
  paginate();
}

void App::paginate() {
  TextPos pos{};
  page_count_ = 0;
  while (pos.paragraph < book_.paragraph_count && page_count_ < kMaxPages) {
    page_starts_[page_count_] = pos;
    const TextPos next =
        typeset_page(page_style(fb_, page_count_), book_.paragraphs, book_.paragraph_count, pos, nullptr);
    ++page_count_;
    if (next == pos) break;  // nothing fits; avoid looping forever
    pos = next;
  }
}

void App::draw_page() {
  fb_.clear(Color::White);
  const int w = fb_.width();
  const int h = fb_.height();

  // Running header: book title, battery.
  draw_text(fb_, fonts::literata_italic_20, kSide * 64, kHeaderBase, book_.title);
  draw_battery(fb_, w - kSide, kHeaderBase, board_.power.battery_percent());

  if (page_ == 0) {
    draw_centered(fb_, fonts::literata_italic_20, kTextTop + 40, book_.chapter_label);
    draw_centered(fb_, fonts::literata_italic_26, kTextTop + 80, book_.chapter_title);
  }

  typeset_page(page_style(fb_, page_), book_.paragraphs, book_.paragraph_count, page_starts_[page_], &fb_);

  // Footer: dotted progress track and page number.
  const int track_y = h - kTrackRise;
  const int track_w = w - 2 * kSide;
  for (int x = kSide; x < w - kSide; x += 4) fb_.set_pixel(x, track_y, Color::Black);
  fb_.fill_rect({kSide, track_y - 1, track_w * (page_ + 1) / page_count_, 3}, Color::Black);
  char label[24];
  std::snprintf(label, sizeof(label), "%d of %d", page_ + 1, page_count_);
  draw_centered(fb_, fonts::literata_20, h - kFooterBase, label);
}

void App::start() {
  draw_page();
  queue_.submit_full();
}

bool App::handle(const InputEvent& ev) {
  if (ev.type == InputEvent::Type::Quit) return false;
  if (ev.type != InputEvent::Type::KeyDown) return true;

  switch (ev.key) {
    case Key::Next:
      if (page_ + 1 >= page_count_) return true;
      ++page_;
      break;
    case Key::Prev:
      if (page_ == 0) return true;
      --page_;
      break;
    case Key::Home:
      queue_.submit_full();
      return true;
    case Key::Power:
      return true;
  }
  draw_page();
  queue_.submit();
  return true;
}

void run(Board& board) {
  static App app(board);  // too large for a small task stack
  app.start();
  for (;;) {
    InputEvent ev;
    if (!board.input.wait(ev, 60 * 1000)) continue;
    if (!app.handle(ev)) return;
  }
}

}  // namespace vellum

// Reader: justified Literata text over a footer with progress, time and battery.
// No header: the page belongs to the text.
#include "vellum/reader.h"

#include <cstdio>

#include "vellum/ui.h"

namespace vellum {
namespace {

constexpr int kTextTop = 44;       // top of the text block
constexpr int kTextBottom = 70;    // space below the text block
constexpr int kFooterBase = 28;    // footer baseline, from the bottom edge
constexpr int kTrackRise = 56;     // progress track, from the bottom edge
constexpr int kOpeningDrop = 140;  // extra space above the text on a chapter's first page

const Font& body() { return fonts::literata_26; }

PageStyle page_style(const Framebuffer& fb, const Book& book, int page) {
  const int top = kTextTop + (page == 0 ? kOpeningDrop : 0);
  return {&body(),
          {ui::kSide, top, fb.width() - 2 * ui::kSide, fb.height() - top - kTextBottom},
          body().size,
          hyphenator_for(book.language)};
}

bool before_or_at(TextPos a, TextPos b) {
  return a.paragraph < b.paragraph || (a.paragraph == b.paragraph && a.offset <= b.offset);
}

}  // namespace

void Reader::open(const Book& book, TextPos position, const Framebuffer& fb) {
  if (book_ != &book) {
    book_ = &book;
    paginate(fb);
  }
  page_ = 0;
  while (page_ + 1 < page_count_ && before_or_at(page_starts_[page_ + 1], position)) ++page_;
}

void Reader::paginate(const Framebuffer& fb) {
  const Book& book = *book_;
  TextPos pos{};
  page_count_ = 0;
  while (pos.paragraph < book.paragraph_count && page_count_ < kMaxPages) {
    page_starts_[page_count_] = pos;
    const TextPos next =
        typeset_page(page_style(fb, book, page_count_), book.paragraphs, book.paragraph_count, pos, nullptr);
    ++page_count_;
    if (next == pos) break;  // nothing fits; avoid looping forever
    pos = next;
  }
  if (page_count_ == 0) page_starts_[page_count_++] = {};
}

bool Reader::next() {
  if (page_ + 1 >= page_count_) return false;
  ++page_;
  return true;
}

bool Reader::prev() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

void Reader::draw(Framebuffer& fb, std::string_view time, int battery_percent) const {
  const Book& book = *book_;
  fb.clear(Color::White);
  const int w = fb.width();
  const int h = fb.height();

  if (page_ == 0) {
    if (book.chapter_title[0] != '\0') {
      ui::draw_centered(fb, fonts::literata_italic_20, kTextTop + 56, book.chapter_label);
      ui::draw_centered(fb, fonts::literata_italic_26, kTextTop + 96, book.chapter_title);
    } else {
      ui::draw_centered(fb, fonts::literata_italic_26, kTextTop + 80, book.chapter_label);
    }
  }

  typeset_page(page_style(fb, book, page_), book.paragraphs, book.paragraph_count, page_starts_[page_], &fb);

  // Footer: dotted progress track, then time, page number and battery.
  ui::draw_progress(fb, ui::kSide, h - kTrackRise, w - 2 * ui::kSide, page_ + 1, page_count_);
  draw_text(fb, fonts::literata_16, ui::kSide * 64, h - kFooterBase, time);
  char label[24];
  std::snprintf(label, sizeof(label), "%d of %d", page_ + 1, page_count_);
  ui::draw_centered(fb, fonts::literata_16, h - kFooterBase, label);
  ui::draw_battery(fb, w - ui::kSide, h - kFooterBase, battery_percent);
}

}  // namespace vellum

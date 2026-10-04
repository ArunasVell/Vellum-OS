// Reader: Literata text over a slim footer with progress, time and battery.
// No header: the page belongs to the text.
#include "vellum/reader.h"

#include <cstdio>

#include "vellum/ui.h"

namespace vellum {
namespace {

constexpr int kTextTop = 36;       // top of the text block
constexpr int kTextBottom = 44;    // space below the text block, for the footer
constexpr int kFooterBase = 12;    // footer baseline, from the bottom edge
constexpr int kTrackRise = 32;     // progress track, from the bottom edge
constexpr int kOpeningDrop = 140;  // extra space above the text on a chapter's first page

const Font* const kSizes[] = {&fonts::literata_23, &fonts::literata_26, &fonts::literata_30, &fonts::literata_34};
constexpr int kLinePercent[] = {90, 100, 115};
constexpr int kWordSpace[] = {80, 100, 125};
constexpr int kMargins[] = {16, ui::kSide, 44};

bool before_or_at(TextPos a, TextPos b) {
  return a.paragraph < b.paragraph || (a.paragraph == b.paragraph && a.offset <= b.offset);
}

}  // namespace

ReadingStyle reading_style(const Settings& s) {
  return {kSizes[static_cast<int>(s.text_size)],
          kLinePercent[static_cast<int>(s.line_spacing)],
          kWordSpace[static_cast<int>(s.word_spacing)],
          kMargins[static_cast<int>(s.margins)],
          s.justify,
          s.hyphenate};
}

PageStyle page_style(const ReadingStyle& style, const Framebuffer& fb, const Book& book, bool first_page) {
  const Font& font = *style.font;
  const int top = kTextTop + (first_page ? kOpeningDrop : 0);
  PageStyle p{&font,
              {style.margin, top, fb.width() - 2 * style.margin, fb.height() - top - kTextBottom},
              font.size,
              style.hyphenate ? hyphenator_for(book.language) : nullptr};
  p.line_height = font.line_height * style.line_percent / 100;
  p.word_space = style.word_space;
  p.justify = style.justify;
  return p;
}

void Reader::open(const Book& book, const ReadingStyle& style, TextPos position, const Framebuffer& fb) {
  if (book_ != &book || !(style_ == style)) {
    book_ = &book;
    style_ = style;
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
    const TextPos next = typeset_page(page_style(style_, fb, book, page_count_ == 0), book.paragraphs,
                                      book.paragraph_count, pos, nullptr);
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

void Reader::draw(Framebuffer& fb, const Strings& text, std::string_view time, int battery_percent) const {
  const Book& book = *book_;
  fb.clear(Color::White);
  const int w = fb.width();
  const int h = fb.height();

  if (page_ == 0) {
    if (book.chapter_title[0] != '\0') {
      ui::draw_centered(fb, fonts::literata_italic_20, kTextTop + 56, book.chapter_label);
      ui::draw_wrapped(fb, fonts::literata_italic_26, style_.margin, kTextTop + 96, w - 2 * style_.margin, 1,
                       book.chapter_title, true);
    } else {
      ui::draw_centered(fb, fonts::literata_italic_26, kTextTop + 80, book.chapter_label);
    }
  }

  typeset_page(page_style(style_, fb, book, page_ == 0), book.paragraphs, book.paragraph_count, page_starts_[page_],
               &fb);

  // Footer: dotted progress track, then time, page number and battery on one low line.
  ui::draw_progress(fb, style_.margin, h - kTrackRise, w - 2 * style_.margin, page_ + 1, page_count_);
  draw_text(fb, fonts::literata_16, style_.margin * 64, h - kFooterBase, time);
  char label[32];
  std::snprintf(label, sizeof(label), text.short_of, page_ + 1, page_count_);
  ui::draw_centered(fb, fonts::literata_16, h - kFooterBase, label);
  ui::draw_battery(fb, w - style_.margin, h - kFooterBase, battery_percent);
}

}  // namespace vellum

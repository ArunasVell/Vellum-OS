// Reader: Literata text over a slim footer with progress, time and battery.
// No header: the page belongs to the text.
#include "vellum/reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/ui.h"

namespace vellum {
namespace {

constexpr int kTextTop = 36;       // top of the text block
constexpr int kTextBottom = 44;    // space below the text block, for the footer
constexpr int kFooterBase = 12;    // footer baseline, from the bottom edge
constexpr int kTrackRise = 32;     // progress track, from the bottom edge
constexpr int kOpeningDrop = 140;  // extra space above the text on a chapter's opening page
constexpr int kRibbonW = 16;       // bookmark ribbon
constexpr int kRibbonH = 30;

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

PageStyle page_style(const ReadingStyle& style, const Framebuffer& fb, const char* language, bool opening) {
  const Font& font = *style.font;
  const int top = kTextTop + (opening ? kOpeningDrop : 0);
  PageStyle p{&font,
              {style.margin, top, fb.width() - 2 * style.margin, fb.height() - top - kTextBottom},
              font.size,
              style.hyphenate ? hyphenator_for(language) : nullptr};
  p.line_height = font.line_height * style.line_percent / 100;
  p.word_space = style.word_space;
  p.justify = style.justify;
  return p;
}

bool Reader::open(Document& doc, uint32_t id, const ReadingStyle& style, BookPos pos, const Framebuffer& fb) {
  const bool same = doc_ == &doc && id_ == id && style_ == style && chapter_ == pos.chapter && fb_ == &fb;
  doc_ = &doc;
  id_ = id;
  style_ = style;
  fb_ = &fb;
  if (!same) {
    chapter_ = -1;
    if (!load(pos.chapter < doc.chapter_count() ? pos.chapter : 0)) return false;
  }
  page_ = 0;
  while (page_ + 1 < page_count_ && before_or_at(page_starts_[page_ + 1], pos.text)) ++page_;
  return true;
}

bool Reader::load(int chapter) {
  if (!doc_->load_chapter(chapter, chapter_info_)) return false;
  chapter_ = chapter;
  paginate();
  page_ = 0;
  return true;
}

void Reader::paginate() {
  const Chapter& ch = chapter_info_;
  TextPos pos{};
  page_count_ = 0;
  while (pos.paragraph < ch.paragraph_count && page_count_ < kMaxPages) {
    page_starts_[page_count_] = pos;
    const bool opening = page_count_ == 0 && ch.label[0] != '\0';
    const TextPos next = typeset_page(page_style(style_, *fb_, doc_->language(), opening), ch.paragraphs,
                                      ch.paragraph_count, pos, nullptr);
    ++page_count_;
    if (next == pos) break;  // nothing fits; avoid looping forever
    pos = next;
  }
  if (page_count_ == 0) page_starts_[page_count_++] = {};
}

bool Reader::next() {
  if (page_ + 1 < page_count_) {
    ++page_;
    return true;
  }
  return chapter_ + 1 < doc_->chapter_count() && load(chapter_ + 1);
}

bool Reader::prev() {
  if (page_ > 0) {
    --page_;
    return true;
  }
  if (chapter_ == 0 || !load(chapter_ - 1)) return false;
  page_ = page_count_ - 1;
  return true;
}

int Reader::compare_to_page(BookPos pos) const {
  if (pos.chapter != chapter_) return pos.chapter < chapter_ ? -1 : 1;
  if (!before_or_at(page_starts_[page_], pos.text)) return -1;
  if (page_ + 1 < page_count_ && before_or_at(page_starts_[page_ + 1], pos.text)) return 1;
  return 0;
}

void Reader::snippet(char* out, size_t size) const {
  if (size < 8) {
    if (size > 0) out[0] = '\0';
    return;
  }
  // The page's text from its first word, paragraphs joined by spaces.
  const Chapter& ch = chapter_info_;
  const TextPos start = page_starts_[page_];
  // Headings are left out unless the page has nothing else: the text says
  // more about which page it was.
  size_t n = 0;
  bool cut = false;
  for (int pass = 0; pass < 2 && n == 0; ++pass) {
    for (uint32_t i = start.paragraph; i < ch.paragraph_count && !cut; ++i) {
      const char* p = ch.paragraphs[i];
      if (*p == kHeadingMark) {
        if (pass == 0) continue;
        ++p;
      }
      if (i == start.paragraph && start.offset <= std::strlen(p)) p += start.offset;
      if (*p == '\0') continue;
      if (n > 0) out[n++] = ' ';
      while (*p != '\0' && n < size - 1) out[n++] = *p++;
      cut = *p != '\0' || n >= size - 1;
    }
  }
  out[n] = '\0';
  if (!cut) return;
  // Shorten to the last whole word that leaves room for the ellipsis.
  size_t end = std::min(n, size - 4);
  while (end > 0 && out[end] != ' ') --end;
  if (end == 0) {
    end = size - 4;
    while (end > 0 && (static_cast<uint8_t>(out[end]) & 0xC0) == 0x80) --end;  // a whole character
  }
  std::memcpy(out + end, "\xE2\x80\xA6", 4);  // …
}

int Reader::percent() const {
  uint64_t before = 0;
  uint64_t total = 0;
  for (int i = 0; i < doc_->chapter_count(); ++i) {
    const uint32_t w = doc_->chapter_weight(i);
    if (i < chapter_) before += w;
    total += w;
  }
  if (total == 0) return 0;
  const uint64_t done = before * page_count_ + uint64_t{doc_->chapter_weight(chapter_)} * (page_ + 1);
  return static_cast<int>(done * 100 / (total * page_count_));
}

void Reader::draw(Framebuffer& fb, const Strings& text, std::string_view time, int battery_percent,
                  bool bookmarked) const {
  const Chapter& ch = chapter_info_;
  fb.clear(Color::White);
  const int w = fb.width();
  const int h = fb.height();

  if (opening()) {
    if (ch.title[0] != '\0') {
      ui::draw_centered(fb, fonts::literata_italic_20, kTextTop + 56, ch.label);
      ui::draw_wrapped(fb, fonts::literata_italic_26, style_.margin, kTextTop + 96, w - 2 * style_.margin, 1, ch.title,
                       true);
    } else {
      ui::draw_centered(fb, fonts::literata_italic_26, kTextTop + 80, ch.label);
    }
  }

  typeset_page(page_style(style_, fb, doc_->language(), opening()), ch.paragraphs, ch.paragraph_count,
               page_starts_[page_], &fb);

  // Footer: dotted progress track through the whole book, then time, page
  // number within the chapter and battery on one low line.
  ui::draw_progress(fb, style_.margin, h - kTrackRise, w - 2 * style_.margin, percent(), 100);
  draw_text(fb, fonts::literata_16, style_.margin * 64, h - kFooterBase, time);
  char label[32];
  std::snprintf(label, sizeof(label), text.short_of, page_ + 1, page_count_);
  ui::draw_centered(fb, fonts::literata_16, h - kFooterBase, label);
  ui::draw_battery(fb, w - style_.margin, h - kFooterBase, battery_percent);

  if (bookmarked) {
    // A ribbon hanging from the top edge, notched at the end.
    const int x = w - style_.margin - kRibbonW;
    fb.fill_rect({x, 0, kRibbonW, kRibbonH}, Color::Black);
    for (int r = 0; r < kRibbonW / 2; ++r) {
      const int half = r + 1;
      fb.hline(x + kRibbonW / 2 - half, kRibbonH - kRibbonW / 2 + r, 2 * half, Color::White);
    }
  }
}

}  // namespace vellum

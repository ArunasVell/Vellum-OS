// Reader: text in the chosen book font over a slim footer with progress, time and battery.
// No header: the page belongs to the text.
#include "vellum/reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

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

constexpr int kTextPx[] = {23, 26, 30, 34};
constexpr int kLinePercent[] = {90, 100, 115};
constexpr int kWordSpace[] = {80, 100, 125};
constexpr int kMargins[] = {16, ui::kSide, 44};

bool before_or_at(TextPos a, TextPos b) {
  return a.paragraph < b.paragraph || (a.paragraph == b.paragraph && a.offset <= b.offset);
}

}  // namespace

int text_px(TextSize size) { return kTextPx[static_cast<int>(size)]; }

ReadingStyle reading_style(const Settings& s, const BookFonts* fonts) {
  const BookFonts f = fonts != nullptr ? *fonts : literata_book(text_px(s.text_size));
  return {f.body,
          f.heading_small,
          f.heading,
          kLinePercent[static_cast<int>(s.line_spacing)],
          kWordSpace[static_cast<int>(s.word_spacing)],
          kMargins[static_cast<int>(s.margins)],
          s.justify,
          s.hyphenate,
          f.generation,
          s.publisher_styles,
          static_cast<IndentRule>(s.paragraph_indent),
          s.focus_reading};
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
  p.publisher = style.publisher;
  p.indent_rule = style.indent_rule;
  p.focus = style.focus;
  return p;
}

PageStyle Reader::layout_style(const Framebuffer& fb, bool opening_page) const {
  PageStyle p = page_style(style_, fb, doc_->language(), opening_page);
  const ChapterMarkup m = doc_->markup();
  p.runs = m.runs;
  p.run_count = m.run_count;
  p.pictures = m.pictures;
  p.picture_count = m.picture_count;
  p.draw_picture = &Reader::draw_picture;
  p.picture_ctx = const_cast<Reader*>(this);
  return p;
}

bool Reader::draw_picture(void* ctx, Framebuffer& fb, const Picture& p, Rect r) {
  auto* reader = static_cast<Reader*>(ctx);
  if (!reader->doc_->draw_picture(fb, p, r)) return false;
  if (reader->shown_count_ < kMaxShown) reader->shown_[reader->shown_count_++] = r;
  return true;
}

Reader::Reader() : page_starts_(new(std::nothrow) TextPos[kMaxPages]) {}

Reader::~Reader() { delete[] page_starts_; }

bool Reader::open(Document& doc, uint32_t id, const ReadingStyle& style, BookPos pos, const Framebuffer& fb) {
  if (page_starts_ == nullptr) return false;
  const bool same_book = doc_ == &doc && id_ == id && style_ == style && fb_ == &fb;
  const bool same = same_book && chapter_ == pos.chapter;
  doc_ = &doc;
  id_ = id;
  style_ = style;
  fb_ = &fb;
  words_page_ = -1;  // the style or book may differ
  if (!same) {
    if (!same_book) sample_pages_ = 0;  // another book's pages say nothing about this one
    chapter_ = -1;
    if (!load(pos.chapter < doc.chapter_count() ? pos.chapter : 0)) return false;
  }
  page_ = 0;
  while (page_ + 1 < page_count_ && before_or_at(page_starts_[page_ + 1], pos.text)) ++page_;
  return true;
}

bool Reader::load(int chapter) {
  words_page_ = -1;
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
    const TextPos next = typeset_page(layout_style(*fb_, opening), ch.paragraphs, ch.paragraph_count, pos, nullptr);
    ++page_count_;
    if (next == pos) break;  // nothing fits; avoid looping forever
    pos = next;
  }
  if (page_count_ == 0) page_starts_[page_count_++] = {};
  if (page_count_ > sample_pages_) {
    sample_pages_ = page_count_;
    sample_weight_ = doc_->chapter_weight(chapter_);
  }
}

bool Reader::next() {
  if (page_ + 1 < page_count_) {
    ++page_;
    return true;
  }
  return chapter_ + 1 < doc_->chapter_count() && load(chapter_ + 1);
}

bool Reader::go_to_percent(int percent) {
  percent = std::clamp(percent, 0, 100);
  uint64_t total = 0;
  for (int i = 0; i < doc_->chapter_count(); ++i) total += doc_->chapter_weight(i);
  const int c = chapter_at(percent);
  if (total == 0 || (c != chapter_ && !load(c))) return false;
  uint64_t before = 0;
  for (int i = 0; i < c; ++i) before += doc_->chapter_weight(i);
  // The page whose percent() is the one asked for (the reverse of it).
  const uint64_t want = total * static_cast<uint64_t>(percent) / 100;
  const uint64_t into = want > before ? want - before : 0;
  const uint64_t weight = doc_->chapter_weight(c);
  const int64_t p = static_cast<int64_t>((into * static_cast<uint64_t>(page_count_) + weight - 1) / weight) - 1;
  page_ = static_cast<int>(std::clamp<int64_t>(p, 0, page_count_ - 1));
  words_page_ = -1;
  return true;
}

int Reader::chapter_at(int percent) const {
  uint64_t total = 0;
  for (int i = 0; i < doc_->chapter_count(); ++i) total += doc_->chapter_weight(i);
  const uint64_t want = total * static_cast<uint64_t>(std::clamp(percent, 0, 100)) / 100;
  uint64_t before = 0;
  int c = 0;
  for (; c + 1 < doc_->chapter_count(); ++c) {
    const uint32_t w = doc_->chapter_weight(c);
    if (before + w > want) break;
    before += w;
  }
  return c;
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
      const ParagraphMarks marks = read_marks(p);
      if (marks.picture || marks.rule) continue;
      if (marks.heading && pass == 0) continue;
      if (i == start.paragraph && start.offset <= std::strlen(p)) p += start.offset;
      if (static_cast<size_t>(p - ch.paragraphs[i]) < marks.length) p = ch.paragraphs[i] + marks.length;
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

const PageWords& Reader::words() const {
  if (words_page_ != page_ || words_chapter_ != chapter_ || words_doc_ != doc_) {
    typeset_page(layout_style(*fb_, opening()), chapter_info_.paragraphs, chapter_info_.paragraph_count,
                 page_starts_[page_], nullptr, &words_);
    words_page_ = page_;
    words_chapter_ = chapter_;
    words_doc_ = doc_;
  }
  return words_;
}

void Reader::word_text(int i, char* out, size_t size) const {
  if (size == 0) return;
  out[0] = '\0';
  const PageWords& w = words();
  size_t n = 0;
  // A word hyphenated across lines is its boxes joined.
  for (; i >= 0 && i < w.count; ++i) {
    const WordBox& b = w.words[i];
    const char* p = chapter_info_.paragraphs[b.begin.paragraph];
    for (uint32_t k = b.begin.offset; k < b.end && n + 1 < size; ++k) out[n++] = p[k];
    if (!b.hyphen) break;
  }
  out[n] = '\0';
}

void Reader::passage(TextPos begin, TextPos end, char* out, size_t size) const {
  if (size < 8) {
    if (size > 0) out[0] = '\0';
    return;
  }
  const Chapter& ch = chapter_info_;
  size_t n = 0;
  bool cut = false;
  for (uint32_t i = begin.paragraph; i <= end.paragraph && i < ch.paragraph_count && !cut; ++i) {
    const char* p = ch.paragraphs[i];
    const uint32_t len = static_cast<uint32_t>(std::strlen(p));
    uint32_t from = i == begin.paragraph ? std::min(begin.offset, len) : 0;
    const uint32_t to = i == end.paragraph ? std::min(end.offset, len) : len;
    const ParagraphMarks marks = read_marks(p);
    if (marks.picture || marks.rule) continue;
    from = std::max(from, marks.length);
    if (from >= to) continue;
    if (n > 0) out[n++] = ' ';
    for (uint32_t k = from; k < to; ++k) {
      if (n >= size - 1) {
        cut = true;
        break;
      }
      out[n++] = p[k];
    }
  }
  out[n] = '\0';
  if (!cut) return;
  // Shorten to a whole character that leaves room for the ellipsis.
  size_t at = size - 4;
  while (at > 0 && (static_cast<uint8_t>(out[at]) & 0xC0) == 0x80) --at;
  std::memcpy(out + at, "\xE2\x80\xA6", 4);  // …
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

int Reader::pages_left_in_book() const {
  uint64_t later = 0;
  for (int i = chapter_ + 1; i < doc_->chapter_count(); ++i) later += doc_->chapter_weight(i);
  if (sample_weight_ == 0) return pages_left_in_chapter();
  return pages_left_in_chapter() + static_cast<int>((later * sample_pages_ + sample_weight_ / 2) / sample_weight_);
}

namespace {

bool marked(const TextMark& m, const WordBox& b) { return !(b.begin < m.begin) && b.begin < m.end; }

// A dotted line, a pixel on and a pixel off, two rows thick.
void dotted_line(Framebuffer& fb, int x, int y, int w) {
  for (int i = 0; i < w; i += 2) {
    fb.set_pixel(x + i, y, Color::Black);
    fb.set_pixel(x + i + 1, y + 1, Color::Black);
  }
}

}  // namespace

void Reader::draw(Framebuffer& fb, const Strings& text, std::string_view time, int battery_percent, bool bookmarked,
                  std::string_view middle, std::span<const TextMark> marks) const {
  const Chapter& ch = chapter_info_;
  fb.clear(Color::White);
  const int w = fb.width();
  const int h = fb.height();

  if (opening()) {
    if (ch.title[0] != '\0') {
      ui::draw_centered(fb, *style_.heading_small, kTextTop + 56, ch.label);
      ui::draw_wrapped(fb, *style_.heading, style_.margin, kTextTop + 96, w - 2 * style_.margin, 1, ch.title, true);
    } else {
      ui::draw_centered(fb, *style_.heading, kTextTop + 80, ch.label);
    }
  }

  shown_count_ = 0;
  typeset_page(layout_style(fb, opening()), ch.paragraphs, ch.paragraph_count, page_starts_[page_], &fb,
               marks.empty() ? nullptr : &words_);
  if (!marks.empty()) {
    words_page_ = page_;
    words_chapter_ = chapter_;
    words_doc_ = doc_;
    // Marks under (or around) the words they cover. Underlines run on across
    // the space to the next word when it is marked too, so a passage reads
    // as one line.
    const int under = std::max(3, style_.font->descent / 3);
    for (const TextMark& m : marks) {
      for (int i = 0; i < words_.count; ++i) {
        const WordBox& b = words_.words[i];
        if (!marked(m, b)) continue;
        int right = b.x + b.w;
        if (i + 1 < words_.count && words_.words[i + 1].baseline == b.baseline && marked(m, words_.words[i + 1])) {
          right = words_.words[i + 1].x;
        }
        const int y = b.baseline + under;
        switch (m.style) {
          case TextMark::Style::Underline:
            fb.fill_rect({b.x, y, right - b.x, 2}, Color::Black);
            break;
          case TextMark::Style::Dotted:
            dotted_line(fb, b.x, y, right - b.x);
            break;
          case TextMark::Style::Boxed:
            fb.draw_rect({b.x - 3, b.y - 2, b.w + 6, b.h + 4}, Color::Black);
            break;
          case TextMark::Style::Selected: {
            // Inverted: white letters on a black, slightly rounded patch.
            const Rect r{b.x - 4, b.y - 2, b.w + 8, b.h + 4};
            fb.fill_round_rect(r, 4, Color::Black);
            const char* p = ch.paragraphs[b.begin.paragraph];
            const int32_t after = draw_text(fb, *style_.font, b.pen, b.baseline,
                                            std::string_view(p + b.begin.offset, b.end - b.begin.offset), Color::White);
            if (b.hyphen) draw_text(fb, *style_.font, after, b.baseline, "-", Color::White);
            break;
          }
        }
      }
    }
  }

  // Footer: dotted progress track through the whole book, then time, page
  // number within the chapter (or what Settings › Footer asks for) and
  // battery on one low line.
  ui::draw_progress(fb, style_.margin, h - kTrackRise, w - 2 * style_.margin, percent(), 100);
  draw_text(fb, ui::label_font(), style_.margin * 64, h - kFooterBase, time);
  char label[32];
  std::snprintf(label, sizeof(label), text.short_of, page_ + 1, page_count_);
  ui::draw_centered(fb, ui::label_font(), h - kFooterBase, middle.empty() ? std::string_view(label) : middle);
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

  // Night: white on black, all but the pictures.
  if (night_) {
    fb.invert({0, 0, w, h});
    for (int i = 0; i < shown_count_; ++i) fb.invert(shown_[i]);
  }
}

}  // namespace vellum

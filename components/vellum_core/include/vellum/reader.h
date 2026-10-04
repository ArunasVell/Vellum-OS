// Reading view: paginates one book and draws its pages.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/book.h"
#include "vellum/document.h"
#include "vellum/font_library.h"
#include "vellum/fonts.h"
#include "vellum/framebuffer.h"
#include "vellum/settings.h"
#include "vellum/strings.h"
#include "vellum/text.h"

namespace vellum {

// How pages are set: everything in Settings that changes the layout.
struct ReadingStyle {
  const Font* font;
  const Font* heading_small;  // chapter label on a chapter's opening page
  const Font* heading;        // chapter title
  int line_percent;           // baseline distance, percent of the font's line height
  int word_space;             // narrowest space between words, percent of the font's space
  int margin;                 // left and right, px
  bool justify;
  bool hyphenate;

  bool operator==(const ReadingStyle&) const = default;
};

// Body text size in pixels for each Settings text size.
int text_px(TextSize size);

// The style the settings ask for, in `fonts` (from Settings › Book font;
// Literata when nullptr).
ReadingStyle reading_style(const Settings& s, const BookFonts* fonts = nullptr);

// Text area for a page: the screen minus margins and footer and, on a
// chapter's opening page, room for its heading. `language` picks hyphenation.
PageStyle page_style(const ReadingStyle& style, const Framebuffer& fb, const char* language, bool opening);

// Where a reader is in a book.
struct BookPos {
  uint16_t chapter = 0;
  TextPos text;
};

class Reader {
 public:
  static constexpr int kMaxPages = 1024;  // per chapter

  // Opens `doc` (`id` tells books apart when one Document object is reused)
  // at the page containing `pos`. Lays the chapter out again only when the
  // book, chapter or style changed. Returns false when the chapter cannot
  // be loaded.
  bool open(Document& doc, uint32_t id, const ReadingStyle& style, BookPos pos, const Framebuffer& fb);

  // Draws the current page. The footer shows `time` (may be empty) on the
  // left, `middle` in the middle (the page number when empty) and the
  // battery on the right. A bookmarked page wears a ribbon in its top corner.
  void draw(Framebuffer& fb, const Strings& text, std::string_view time, int battery_percent, bool bookmarked = false,
            std::string_view middle = {}) const;
  bool next();  // false at the end of the book
  bool prev();  // false at the start of the book

  uint32_t id() const { return id_; }
  Document* document() const { return doc_; }
  int chapter() const { return chapter_; }
  int page() const { return page_; }
  int page_count() const { return page_count_; }
  BookPos position() const { return {static_cast<uint16_t>(chapter_), page_starts_[page_]}; }
  // Share of the whole book read, 0..100, weighting chapters by length.
  int percent() const;
  // Pages from this one to the end of the chapter, this one included.
  int pages_left_in_chapter() const { return page_count_ - page_; }
  // The same to the end of the book. Later chapters are not laid out yet:
  // their pages are guessed from their length, at the rate of the longest
  // chapter laid out so far.
  int pages_left_in_book() const;
  // Where `pos` is relative to the current page: -1 before it, 0 on it, 1 after it.
  int compare_to_page(BookPos pos) const;
  // The first words of the current page, ending in an ellipsis when cut short.
  void snippet(char* out, size_t size) const;

 private:
  bool load(int chapter);
  void paginate();
  bool opening() const { return page_ == 0 && chapter_info_.label[0] != '\0'; }

  Document* doc_ = nullptr;
  uint32_t id_ = 0;
  const Framebuffer* fb_ = nullptr;
  ReadingStyle style_{};
  int chapter_ = -1;
  Chapter chapter_info_{};
  TextPos page_starts_[kMaxPages];
  int page_count_ = 0;
  int page_ = 0;
  // Pages per length, from the longest chapter laid out in this book and style.
  int sample_pages_ = 0;
  uint32_t sample_weight_ = 0;
};

}  // namespace vellum

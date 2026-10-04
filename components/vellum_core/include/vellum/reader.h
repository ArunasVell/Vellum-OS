// Reading view: paginates one book and draws its pages.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/book.h"
#include "vellum/fonts.h"
#include "vellum/framebuffer.h"
#include "vellum/settings.h"
#include "vellum/strings.h"
#include "vellum/text.h"

namespace vellum {

// How pages are set: everything in Settings that changes the layout.
struct ReadingStyle {
  const Font* font;
  int line_percent;  // baseline distance, percent of the font's line height
  int word_space;    // narrowest space between words, percent of the font's space
  int margin;        // left and right, px
  bool justify;
  bool hyphenate;

  bool operator==(const ReadingStyle&) const = default;
};

ReadingStyle reading_style(const Settings& s);

// Text area for a page: the screen minus margins, footer and, on a chapter's
// first page, room for the chapter heading.
PageStyle page_style(const ReadingStyle& style, const Framebuffer& fb, const Book& book, bool first_page);

class Reader {
 public:
  static constexpr int kMaxPages = 1024;

  // Lays out `book` in `style` for a screen of fb's size (again only if the
  // book or style changed) and turns to the page that contains `position`.
  void open(const Book& book, const ReadingStyle& style, TextPos position, const Framebuffer& fb);

  // Draws the current page. The footer shows `time` (may be empty) on the
  // left, the page number in the middle and the battery on the right.
  void draw(Framebuffer& fb, const Strings& text, std::string_view time, int battery_percent) const;
  bool next();  // false at the last page
  bool prev();  // false at the first page

  const Book* book() const { return book_; }
  int page() const { return page_; }
  int page_count() const { return page_count_; }
  TextPos position() const { return page_starts_[page_]; }

 private:
  void paginate(const Framebuffer& fb);

  const Book* book_ = nullptr;
  ReadingStyle style_{};
  TextPos page_starts_[kMaxPages];
  int page_count_ = 0;
  int page_ = 0;
};

}  // namespace vellum

// Reading view: paginates one book and draws its pages.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/book.h"
#include "vellum/framebuffer.h"
#include "vellum/text.h"

namespace vellum {

class Reader {
 public:
  static constexpr int kMaxPages = 1024;

  // Lays out `book` for a screen of fb's size and turns to the page that
  // contains `position`.
  void open(const Book& book, TextPos position, const Framebuffer& fb);

  // Draws the current page. The footer shows `time` (may be empty) on the
  // left, the page number in the middle and the battery on the right.
  void draw(Framebuffer& fb, std::string_view time, int battery_percent) const;
  bool next();  // false at the last page
  bool prev();  // false at the first page

  const Book* book() const { return book_; }
  int page() const { return page_; }
  int page_count() const { return page_count_; }
  TextPos position() const { return page_starts_[page_]; }

 private:
  void paginate(const Framebuffer& fb);

  const Book* book_ = nullptr;
  TextPos page_starts_[kMaxPages];
  int page_count_ = 0;
  int page_ = 0;
};

}  // namespace vellum

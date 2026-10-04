// A readable book split into chapters, loaded one chapter at a time.
#pragma once

#include <cstdint>
#include <cstdio>

#include "vellum/book.h"
#include "vellum/text.h"

namespace vellum {

struct Chapter {
  const char* const* paragraphs;  // UTF-8; kHeadingMark first marks a heading
  uint32_t paragraph_count;
  const char* label;  // e.g. "Chapter I"; may be empty
  const char* title;  // may be empty
};

// One line of a book's table of contents.
struct TocEntry {
  static constexpr uint32_t kUnknown = 0xFFFFFFFF;

  uint16_t chapter = 0;           // where it points
  uint8_t depth = 0;              // 0 for top-level entries
  uint32_t paragraph = kUnknown;  // in that chapter; known once it has been loaded
  char title[96] = {};
};

class Document {
 public:
  virtual ~Document() = default;
  virtual const char* language() const = 0;
  virtual int chapter_count() const = 0;
  // Relative length of chapter i, for progress through the whole book.
  virtual uint32_t chapter_weight(int i) const = 0;
  // Makes chapter i current. Its text stays valid until the next load.
  virtual bool load_chapter(int i, Chapter& out) = 0;
  // The table of contents, in reading order. Empty when the book has none.
  virtual int toc_count() const { return 0; }
  virtual const TocEntry& toc(int i) const = 0;
};

// One of the books compiled into the firmware: a single chapter.
class BuiltinDocument : public Document {
 public:
  void set(const Book& book) {
    book_ = &book;
    toc_ = {};
    toc_.paragraph = 0;
    if (book.chapter_title[0] != '\0') {
      std::snprintf(toc_.title, sizeof(toc_.title), "%s \xC2\xB7 %s", book.chapter_label, book.chapter_title);  // ·
    } else {
      std::snprintf(toc_.title, sizeof(toc_.title), "%s", book.chapter_label);
    }
  }
  const char* language() const override { return book_->language; }
  int chapter_count() const override { return 1; }
  uint32_t chapter_weight(int) const override { return 1; }
  bool load_chapter(int i, Chapter& out) override {
    if (i != 0) return false;
    out = {book_->paragraphs, book_->paragraph_count, book_->chapter_label, book_->chapter_title};
    return true;
  }
  int toc_count() const override { return 1; }
  const TocEntry& toc(int) const override { return toc_; }

 private:
  const Book* book_ = nullptr;
  TocEntry toc_;
};

}  // namespace vellum

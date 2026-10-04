// A readable book split into chapters, loaded one chapter at a time.
#pragma once

#include <cstdint>

#include "vellum/book.h"
#include "vellum/text.h"

namespace vellum {

struct Chapter {
  const char* const* paragraphs;  // UTF-8; kHeadingMark first marks a heading
  uint32_t paragraph_count;
  const char* label;  // e.g. "Chapter I"; may be empty
  const char* title;  // may be empty
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
};

// One of the books compiled into the firmware: a single chapter.
class BuiltinDocument : public Document {
 public:
  void set(const Book& book) { book_ = &book; }
  const char* language() const override { return book_->language; }
  int chapter_count() const override { return 1; }
  uint32_t chapter_weight(int) const override { return 1; }
  bool load_chapter(int i, Chapter& out) override {
    if (i != 0) return false;
    out = {book_->paragraphs, book_->paragraph_count, book_->chapter_label, book_->chapter_title};
    return true;
  }

 private:
  const Book* book_ = nullptr;
};

}  // namespace vellum

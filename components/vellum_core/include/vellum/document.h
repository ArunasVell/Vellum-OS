// A readable book split into chapters, loaded one chapter at a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

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

// A link in a chapter's text: the words from `begin` up to `end` lead to
// another place in the book.
struct Link {
  static constexpr uint16_t kThisChapter = 0xFFFF;  // until the chapter's number is known
  TextPos begin;
  TextPos end;
  uint32_t anchor = 0;  // hash of the #fragment it points to (anchor_hash); 0 for a chapter's start
  uint16_t chapter = kThisChapter;
  bool note = false;  // a footnote reference: shown over the page, not followed
};

// FNV-1a of an anchor's id; never 0.
inline uint32_t anchor_hash(std::string_view id) {
  uint32_t h = 2166136261u;
  for (char c : id) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  return h == 0 ? 1 : h;
}

// Where a link leads.
struct LinkTarget {
  uint16_t chapter = 0;
  uint32_t paragraph = 0;
  bool note = false;  // a footnote: show its text rather than go there
};

// What a chapter holds besides its paragraphs, in text order.
struct ChapterMarkup {
  const StyleRun* runs = nullptr;
  uint32_t run_count = 0;
  const Link* links = nullptr;
  uint32_t link_count = 0;
  const Picture* pictures = nullptr;
  uint32_t picture_count = 0;
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
  // Emphasis, links and pictures of the loaded chapter; none by default.
  virtual ChapterMarkup markup() const { return {}; }
  // Where `link` (of the loaded chapter) leads and, for a footnote, its
  // text in `note`. May read other chapters on the way; the loaded one
  // stays loaded. False when the link leads nowhere.
  virtual bool follow(const Link& link, LinkTarget& out, char* note, size_t size) {
    (void)link, (void)out, (void)note, (void)size;
    return false;
  }
  // Draws picture p of the loaded chapter, dithered to fill r. False when
  // it cannot be shown.
  virtual bool draw_picture(Framebuffer& fb, const Picture& p, Rect r) {
    (void)fb, (void)p, (void)r;
    return false;
  }
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

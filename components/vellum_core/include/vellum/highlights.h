// Passages marked while reading, with notes added from the phone.
//
// Kept in vellum/highlights.txt on the card, one per line, tab-separated:
//   id  book path  book title  where  percent  date  passage  note
// where "where" is "chapter:paragraph.offset-paragraph.offset" (the
// passage's first byte and one past its last, in that chapter). The
// reader writes the file; while "Add books" is open the phone page reads
// it and changes notes through the transfer server, which uses the same
// class, and the reader loads it again afterwards.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"
#include "vellum/reader.h"

namespace vellum {

struct Highlight {
  uint32_t id = 0;      // stays the same while it exists; the phone names it by this
  char path[160] = {};  // the book's LibraryEntry::path
  char title[64] = {};  // the book's title, for lists on the phone
  BookPos begin;        // first byte of the passage
  TextPos end;          // one past its last byte, in the same chapter
  uint8_t percent = 0;  // of the whole book
  char date[20] = {};   // "2026-10-04 21:30", or "" when the clock was unknown
  char text[300] = {};  // the passage, cut short with an ellipsis
  char note[240] = {};  // added on the phone; may be empty
};

class Highlights {
 public:
  static constexpr int kMax = 256;
  static constexpr const char* kPath = "vellum/highlights.txt";

  Highlights();
  ~Highlights();
  Highlights(const Highlights&) = delete;
  Highlights& operator=(const Highlights&) = delete;

  // Replaces what is held with the card's file (empty when there is none).
  void load(Storage& storage);
  bool save(Storage& storage) const;

  int count() const { return count_; }
  const Highlight& at(int k) const { return items_[k]; }
  // Index of the highlight with this id, or -1.
  int find(uint32_t id) const;
  // Writes the indices of the book's highlights to `out` in reading order;
  // with chapter >= 0 only those in that chapter. Returns how many.
  int of_book(const char* path, int chapter, int* out, int max) const;

  // Adds one (its id is filled in); returns its index, or -1 when full.
  int add(const Highlight& h);
  void remove(int k);
  // A book's file moved: its highlights follow it to the new path.
  void rename_book(const char* from, const char* to);
  // Sets a note (tabs and line breaks become spaces); false when there is no such id.
  bool set_note(uint32_t id, const char* note);

  // All highlights as JSON for the phone page, in pieces passed to emit:
  //   [{"id":1,"book":"...","title":"...","chapter":0,"percent":12,
  //     "date":"...","text":"...","note":"..."}, ...]
  using EmitFn = void (*)(void* ctx, const char* text, size_t len);
  void write_json(EmitFn emit, void* ctx) const;

 private:
  Highlight* items_;
  int count_ = 0;
  uint32_t next_id_ = 1;
};

}  // namespace vellum

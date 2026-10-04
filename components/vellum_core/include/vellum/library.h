// The books on the device and how far each one has been read.
//
// Kept in RAM for now; it moves to storage once the SD card driver lands.
#pragma once

#include <cstdint>
#include <span>

#include "vellum/book.h"
#include "vellum/text.h"

namespace vellum {

struct BookState {
  TextPos position;          // start of the page last shown
  uint16_t page = 0;         // index of that page
  uint16_t page_count = 0;   // 0 until the book has been opened and paginated
  uint32_t last_opened = 0;  // larger = more recent; 0 = never opened

  bool started() const { return last_opened != 0; }
  // Share read, 0..100, counting the page on screen as read.
  int percent() const { return page_count ? (page + 1) * 100 / page_count : 0; }
};

class Library {
 public:
  static constexpr int kMaxBooks = 64;

  explicit Library(std::span<const Book* const> books);

  int count() const { return count_; }
  const Book& book(int i) const { return *books_[i]; }
  BookState& state(int i) { return states_[i]; }
  const BookState& state(int i) const { return states_[i]; }

  // Records that book i was opened just now.
  void touch(int i);
  // The book read most recently, or the first book when none has been opened.
  int current() const;
  // Writes book indices to `out`, most recently opened first and unopened
  // books after them in library order. Returns how many were written.
  int by_recent(int* out, int max) const;

 private:
  const Book* books_[kMaxBooks];
  BookState states_[kMaxBooks];
  int count_ = 0;
  uint32_t clock_ = 0;
};

}  // namespace vellum

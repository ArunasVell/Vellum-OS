// Reading stats: how long and how fast each book is read, per day.
//
// Time counts while a book is open and its pages are turning. Each page is
// credited with the time it was shown, but never more than kIdleMs: a book
// left open on the table stops counting after that. Leaving the reader or
// sleeping ends the stretch. Page turns forward count as pages read.
//
// Totals are kept per book and, when the clock is set, per day and book for
// the last kKeepDays days. They live in vellum/stats.txt on the card,
// written when the reader is left and every kSaveMs while reading.
#pragma once

#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

class ReadingStats {
 public:
  static constexpr int kMaxBooks = 64;   // the least recently read is forgotten first
  static constexpr int kMaxDays = 1024;  // day-and-book totals; the oldest go first
  static constexpr uint32_t kKeepDays = 365;
  static constexpr uint32_t kIdleMs = 2 * 60 * 1000;
  static constexpr uint32_t kSaveMs = 5 * 60 * 1000;
  static constexpr const char* kPath = "vellum/stats.txt";
  static constexpr uint8_t kNoBook = 0xFF;  // a day entry of a book since forgotten

  struct BookTotal {
    char path[160] = {};  // LibraryEntry::path
    uint32_t seconds = 0;
    uint32_t pages = 0;
    uint32_t last_read = 0;  // larger = more recent
  };
  struct DayTotal {
    uint16_t day = 0;  // days since 2000-01-01
    uint8_t book = kNoBook;
    uint32_t seconds = 0;
    uint32_t pages = 0;
  };

  ReadingStats();
  ~ReadingStats();
  ReadingStats(const ReadingStats&) = delete;
  ReadingStats& operator=(const ReadingStats&) = delete;

  void load(Storage& storage);
  bool save(Storage& storage);
  void clear();

  // Reading `path` from now_ms on (a steady count, see InputSource::now_ms).
  // Carries on when that book is already being timed.
  void start(const char* path, uint32_t now_ms);
  // A page turned at now_ms; `day` is today (days since 2000) or -1 when
  // the clock is not set.
  void turn(uint32_t now_ms, int day, bool forward);
  // The reader was left (or the device sleeps): credits the page shown last.
  void stop(uint32_t now_ms, int day);
  bool running() const { return book_ >= 0; }
  // Something new to write, and kSaveMs since the last write.
  bool save_due(uint32_t now_ms) const { return dirty_ && now_ms - saved_ms_ >= kSaveMs; }
  bool dirty() const { return dirty_; }

  // Totals over days first..last (inclusive), all books.
  uint32_t seconds_between(int first, int last) const;
  uint32_t pages_between(int first, int last) const;
  // Pages an hour: from this book's latest reading when there is enough of
  // it, else from all books. -1 until enough has been read to tell.
  int pages_per_hour(const char* path) const;
  // Pages an hour over everything read.
  int pages_per_hour() const;

  // Books, most recently read first.
  int book_count() const { return book_count_; }
  const BookTotal& book_by_recent(int i) const;
  const BookTotal* find_book(const char* path) const;
  // A book's file moved: its totals follow it to the new path.
  void rename_book(const char* from, const char* to);

  int day_count() const { return day_count_; }
  const DayTotal& day(int i) const { return days_[i]; }

 private:
  int find(const char* path) const;
  int add_book(const char* path);
  void credit(uint32_t ms, int day, uint32_t pages);
  // Pages an hour from day entries newest first (book < 0: every book), then
  // from the totals. -1 when there is too little.
  int speed(int book) const;

  BookTotal* books_;
  int book_count_ = 0;
  uint32_t sequence_ = 0;  // for BookTotal::last_read
  DayTotal* days_;         // oldest first
  int day_count_ = 0;
  char* file_;  // for loading and saving

  int book_ = -1;          // being timed, or -1
  uint32_t mark_ms_ = 0;   // when the page now shown appeared
  uint32_t carry_ms_ = 0;  // the part of a second not credited yet
  bool dirty_ = false;
  uint32_t saved_ms_ = 0;  // when last written
  uint32_t last_ms_ = 0;   // the latest time passed in
};

// Minutes to read `pages` at `pages_per_hour`, at least 1 when there is
// anything left; -1 when the speed is unknown.
int minutes_for_pages(int pages, int pages_per_hour);

// Today as days since 2000-01-01, or -1 when the clock is missing or not set.
int today(Clock* clock);

}  // namespace vellum

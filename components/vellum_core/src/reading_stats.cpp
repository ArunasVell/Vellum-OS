#include "vellum/reading_stats.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

#include "vellum/battery_log.h"
#include "vellum/textfile.h"

namespace vellum {
namespace {

// Room for every book line (path and three numbers) and every day line.
constexpr uint32_t kFileBytes = ReadingStats::kMaxBooks * 200 + ReadingStats::kMaxDays * 40;

// Speed is worked out from about the last two hours of reading, and only
// once there are a few minutes and a few pages to go on.
constexpr uint64_t kSpeedWindowS = 2 * 3600;
constexpr uint64_t kSpeedMinS = 5 * 60;
constexpr uint64_t kSpeedMinPages = 5;

int per_hour(uint64_t seconds, uint64_t pages) {
  if (seconds < kSpeedMinS || pages < kSpeedMinPages) return -1;
  return static_cast<int>((pages * 3600 + seconds / 2) / seconds);
}

// "2026-10-04" -> days since 2000-01-01; -1 when it is not a date.
int parse_day(std::string_view s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return -1;
  const uint32_t y = textfile::to_uint(s.substr(0, 4), 0);
  const uint32_t m = textfile::to_uint(s.substr(5, 2), 0);
  const uint32_t d = textfile::to_uint(s.substr(8, 2), 0);
  if (y < 2000 || y > 2150 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  const DateTime t{static_cast<uint16_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d), 0, 0, 0};
  return static_cast<int>(seconds_since_2000(t) / 86400);
}

}  // namespace

int minutes_for_pages(int pages, int pages_per_hour) {
  if (pages_per_hour <= 0) return -1;
  if (pages <= 0) return 0;
  const int minutes = (pages * 60 + pages_per_hour / 2) / pages_per_hour;
  return minutes < 1 ? 1 : minutes;
}

int today(Clock* clock) {
  DateTime now;
  if (clock == nullptr || !clock->now(now)) return -1;
  return static_cast<int>(seconds_since_2000(now) / 86400);
}

ReadingStats::ReadingStats()
    : books_(new(std::nothrow) BookTotal[kMaxBooks]),
      days_(new(std::nothrow) DayTotal[kMaxDays]),
      file_(new(std::nothrow) char[kFileBytes]) {}

ReadingStats::~ReadingStats() {
  delete[] books_;
  delete[] days_;
  delete[] file_;
}

void ReadingStats::clear() {
  book_count_ = 0;
  day_count_ = 0;
  sequence_ = 0;
  book_ = -1;
  carry_ms_ = 0;
  dirty_ = false;
}

int ReadingStats::find(const char* path) const {
  for (int i = 0; i < book_count_; ++i) {
    if (std::strcmp(books_[i].path, path) == 0) return i;
  }
  return -1;
}

void ReadingStats::rename_book(const char* from, const char* to) {
  const int i = find(from);
  if (i < 0) return;
  std::snprintf(books_[i].path, sizeof(BookTotal::path), "%s", to);
  dirty_ = true;
}

const ReadingStats::BookTotal* ReadingStats::find_book(const char* path) const {
  const int i = find(path);
  return i >= 0 ? &books_[i] : nullptr;
}

int ReadingStats::add_book(const char* path) {
  int slot = book_count_;
  if (book_count_ == kMaxBooks) {
    // Full: the book read longest ago gives up its place. Its days still
    // count towards the daily totals.
    slot = 0;
    for (int i = 1; i < book_count_; ++i) {
      if (books_[i].last_read < books_[slot].last_read) slot = i;
    }
    for (int i = 0; i < day_count_; ++i) {
      if (days_[i].book == slot) days_[i].book = kNoBook;
    }
  } else {
    ++book_count_;
  }
  books_[slot] = {};
  textfile::copy(path, books_[slot].path, sizeof(books_[slot].path));
  return slot;
}

void ReadingStats::start(const char* path, uint32_t now_ms) {
  if (books_ == nullptr || days_ == nullptr) return;
  if (book_ >= 0 && std::strcmp(books_[book_].path, path) == 0) return;  // still reading it
  int b = find(path);
  if (b < 0) b = add_book(path);
  book_ = b;
  books_[b].last_read = ++sequence_;
  mark_ms_ = last_ms_ = now_ms;
  carry_ms_ = 0;
  if (!dirty_) saved_ms_ = now_ms;
}

void ReadingStats::credit(uint32_t ms, int day, uint32_t pages) {
  ms += carry_ms_;
  const uint32_t seconds = ms / 1000;
  carry_ms_ = ms % 1000;
  if (seconds == 0 && pages == 0) return;
  BookTotal& b = books_[book_];
  b.seconds += seconds;
  b.pages += pages;
  dirty_ = true;
  if (day < 0) return;  // the clock is not set: no day to file it under

  // Days long gone make room; so does the oldest when the list is full.
  int drop = 0;
  while (drop < day_count_ && days_[drop].day + static_cast<int>(kKeepDays) < day) ++drop;
  if (drop == 0 && day_count_ == kMaxDays) drop = 1;
  if (drop > 0) {
    std::memmove(days_, days_ + drop, static_cast<size_t>(day_count_ - drop) * sizeof(DayTotal));
    day_count_ -= drop;
  }
  for (int i = day_count_ - 1; i >= 0 && days_[i].day >= day - 1; --i) {
    if (days_[i].day == day && days_[i].book == book_) {
      days_[i].seconds += seconds;
      days_[i].pages += pages;
      return;
    }
  }
  days_[day_count_++] = {static_cast<uint16_t>(day), static_cast<uint8_t>(book_), seconds, pages};
}

void ReadingStats::turn(uint32_t now_ms, int day, bool forward) {
  if (book_ < 0) return;
  const uint32_t shown = now_ms - mark_ms_;
  credit(shown < kIdleMs ? shown : kIdleMs, day, forward ? 1 : 0);
  mark_ms_ = last_ms_ = now_ms;
}

void ReadingStats::stop(uint32_t now_ms, int day) {
  if (book_ < 0) return;
  const uint32_t shown = now_ms - mark_ms_;
  credit(shown < kIdleMs ? shown : kIdleMs, day, 0);
  last_ms_ = now_ms;
  book_ = -1;
  carry_ms_ = 0;
}

uint32_t ReadingStats::seconds_between(int first, int last) const {
  uint32_t total = 0;
  for (int i = 0; i < day_count_; ++i) {
    if (days_[i].day >= first && days_[i].day <= last) total += days_[i].seconds;
  }
  return total;
}

uint32_t ReadingStats::pages_between(int first, int last) const {
  uint32_t total = 0;
  for (int i = 0; i < day_count_; ++i) {
    if (days_[i].day >= first && days_[i].day <= last) total += days_[i].pages;
  }
  return total;
}

int ReadingStats::streak(int day, uint32_t goal_seconds) const {
  if (day < 0) return 0;
  int d = seconds_between(day, day) >= goal_seconds ? day : day - 1;
  int run = 0;
  for (; d >= 0 && run <= static_cast<int>(kKeepDays); --d, ++run) {
    if (seconds_between(d, d) < goal_seconds) break;
  }
  return run;
}

int ReadingStats::speed(int book) const {
  uint64_t seconds = 0, pages = 0;
  for (int i = day_count_ - 1; i >= 0 && seconds < kSpeedWindowS; --i) {
    if (book >= 0 && days_[i].book != book) continue;
    seconds += days_[i].seconds;
    pages += days_[i].pages;
  }
  if (const int r = per_hour(seconds, pages); r >= 0) return r;
  // No clock, or too little by day: the book's (or all books') totals.
  seconds = pages = 0;
  for (int i = 0; i < book_count_; ++i) {
    if (book >= 0 && i != book) continue;
    seconds += books_[i].seconds;
    pages += books_[i].pages;
  }
  return per_hour(seconds, pages);
}

int ReadingStats::pages_per_hour(const char* path) const {
  const int b = find(path);
  if (b >= 0) {
    if (const int r = speed(b); r >= 0) return r;
  }
  return speed(-1);
}

int ReadingStats::pages_per_hour() const {
  uint64_t seconds = 0, pages = 0;
  for (int i = 0; i < book_count_; ++i) {
    seconds += books_[i].seconds;
    pages += books_[i].pages;
  }
  return per_hour(seconds, pages);
}

const ReadingStats::BookTotal& ReadingStats::book_by_recent(int i) const {
  // The i-th largest last_read; the list is short, so a plain count will do.
  for (int b = 0; b < book_count_; ++b) {
    int newer = 0;
    for (int o = 0; o < book_count_; ++o) newer += books_[o].last_read > books_[b].last_read ? 1 : 0;
    if (newer == i) return books_[b];
  }
  return books_[0];
}

void ReadingStats::load(Storage& storage) {
  clear();
  if (books_ == nullptr || days_ == nullptr || file_ == nullptr) return;
  const int32_t n = textfile::read(storage, kPath, file_, kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(file_, static_cast<size_t>(n)), [&](const std::string_view* f, int count) {
    if (f[0] == "b" && count >= 5) {
      if (book_count_ == kMaxBooks || f[4].empty()) return;
      BookTotal& b = books_[book_count_++];
      b = {};
      b.last_read = textfile::to_uint(f[1]);
      b.seconds = textfile::to_uint(f[2]);
      b.pages = textfile::to_uint(f[3]);
      textfile::copy(f[4], b.path, sizeof(b.path));
      if (b.last_read > sequence_) sequence_ = b.last_read;
    } else if (f[0] == "d" && count >= 5 && day_count_ < kMaxDays) {
      // Day lines name books by their place among the book lines.
      const int day = parse_day(f[1]);
      if (day < 0) return;
      const uint32_t book = textfile::to_uint(f[2], kNoBook);
      DayTotal& d = days_[day_count_++];
      d.day = static_cast<uint16_t>(day);
      d.book = book < static_cast<uint32_t>(book_count_) ? static_cast<uint8_t>(book) : kNoBook;
      d.seconds = textfile::to_uint(f[3]);
      d.pages = textfile::to_uint(f[4]);
    }
  });
}

bool ReadingStats::save(Storage& storage) {
  if (file_ == nullptr) return false;
  size_t n = 0;
  for (int i = 0; i < book_count_ && n + 200 < kFileBytes; ++i) {
    const BookTotal& b = books_[i];
    n += static_cast<size_t>(
        std::snprintf(file_ + n, kFileBytes - n, "b\t%lu\t%lu\t%lu\t%s\n", static_cast<unsigned long>(b.last_read),
                      static_cast<unsigned long>(b.seconds), static_cast<unsigned long>(b.pages), b.path));
  }
  for (int i = 0; i < day_count_ && n + 40 < kFileBytes; ++i) {
    const DayTotal& d = days_[i];
    uint16_t y;
    uint8_t m, day;
    date_from_days(d.day, y, m, day);
    char book[4] = "-";
    if (d.book != kNoBook) std::snprintf(book, sizeof(book), "%u", unsigned{d.book});
    n += static_cast<size_t>(std::snprintf(file_ + n, kFileBytes - n, "d\t%04u-%02u-%02u\t%s\t%lu\t%lu\n", unsigned{y},
                                           unsigned{m}, unsigned{day}, book, static_cast<unsigned long>(d.seconds),
                                           static_cast<unsigned long>(d.pages)));
  }
  if (!storage.write(kPath, file_, static_cast<uint32_t>(n))) return false;
  dirty_ = false;
  saved_ms_ = last_ms_;
  return true;
}

}  // namespace vellum

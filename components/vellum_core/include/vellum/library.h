// The books on the device and how far each one has been read: the built-in
// samples plus every EPUB in the card's "books" folder (and its root).
//
// Book details are cached in vellum/library.txt so a restart does not reopen
// every EPUB; reading positions are kept in vellum/progress.txt.
#pragma once

#include <cstdint>
#include <span>

#include "vellum/book.h"
#include "vellum/epub.h"
#include "vellum/hal.h"
#include "vellum/reader.h"

namespace vellum {

struct BookState {
  BookPos position;          // start of the page last shown
  uint8_t percent = 0;       // of the whole book, when last read
  uint32_t last_opened = 0;  // larger = more recent; 0 = never opened

  bool started() const { return last_opened != 0; }
};

struct LibraryEntry {
  const Book* builtin = nullptr;  // set for samples compiled into the firmware
  char path[160] = {};            // card path of an EPUB; "builtin:<title>" for samples
  char title[128] = {};
  char author[96] = {};
  char language[16] = {};
  uint32_t file_size = 0;
  BookState state;
};

class Library {
 public:
  static constexpr int kMaxBooks = 256;
  static constexpr const char* kProgressPath = "vellum/progress.txt";
  static constexpr const char* kCachePath = "vellum/library.txt";

  Library();
  ~Library();
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  // Rebuilds the list: built-in samples, then the card's EPUBs (opened with
  // `epub` only when the cache does not know them), sorted by title. Reading
  // positions are reloaded from the card.
  void load(std::span<const Book* const> builtins, Storage* storage, Epub& epub);
  // Writes reading positions to the card.
  bool save_progress(Storage& storage) const;

  int count() const { return count_; }
  const LibraryEntry& entry(int i) const { return entries_[i]; }
  BookState& state(int i) { return entries_[i].state; }
  const BookState& state(int i) const { return entries_[i].state; }

  // Records that book i was opened just now.
  void touch(int i);
  // The book read most recently, or the first book when none has been opened.
  int current() const;
  // Writes book indices to `out`, most recently opened first and unopened
  // books after them in library order. Returns how many were written.
  int by_recent(int* out, int max) const;

 private:
  void scan_folder(Storage& storage, const char* dir, Epub& epub);
  void load_cache(Storage& storage);
  void load_progress(Storage& storage);

  LibraryEntry* entries_;
  int count_ = 0;
  LibraryEntry* cache_;  // details read from library.txt
  int cache_count_ = 0;
  bool cache_dirty_ = false;
  uint32_t clock_ = 0;
};

}  // namespace vellum

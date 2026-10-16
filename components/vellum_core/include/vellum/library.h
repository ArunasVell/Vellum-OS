// The books on the device and how far each one has been read: the built-in
// samples plus every book in the card's "books" folder and its root, and in
// their folders two levels down (Library::kMaxFolderDepth): EPUB, plain
// text, FictionBook (also zipped) and single web pages.
//
// Book details are cached in vellum/library.txt so a restart does not reopen
// every book, with the order books were first seen in (for "recently
// added"); reading positions (and which books are finished) are kept in
// vellum/progress.txt. Cover thumbnails are made when a book is first seen
// (vellum/covers).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "vellum/book.h"
#include "vellum/epub.h"
#include "vellum/hal.h"
#include "vellum/reader.h"
#include "vellum/settings.h"
#include "vellum/text_book.h"

namespace vellum {

// Whether a file is a book by its name: .epub, or one TextBook reads
// (TextBook::format_of; any case).
bool is_book_file(const char* name);
// Whether a file is a picture the viewer shows: .jpg, .jpeg, .png or .bmp.
bool is_picture_file(const char* name);

struct BookState {
  BookPos position;          // start of the page last shown
  uint8_t percent = 0;       // of the whole book, when last read
  uint32_t last_opened = 0;  // larger = more recent; 0 = never opened
  bool finished = false;     // read to the last page, or marked so

  bool started() const { return last_opened != 0; }
  // Opened and not finished: the library's "Reading now".
  bool reading() const { return started() && !finished; }
};

struct LibraryEntry {
  const Book* builtin = nullptr;  // set for samples compiled into the firmware
  char path[160] = {};            // card path of the book file; "builtin:<title>" for samples
  char title[128] = {};
  char author[96] = {};
  char language[16] = {};
  uint32_t file_size = 0;
  int8_t cover = -1;   // -1 not looked at yet, 0 none, 1 thumbnails on the card (covers::path_for)
  uint32_t added = 0;  // order first seen on the card, larger = newer; 0 for the samples
  BookState state;
};

// A page the reader asked to come back to.
struct Bookmark {
  char path[160] = {};    // the book's LibraryEntry::path
  BookPos position;       // start of the page
  uint8_t percent = 0;    // of the whole book
  char snippet[80] = {};  // the page's first words
};

class Library {
 public:
  static constexpr int kMaxBooks = 256;
  static constexpr const char* kProgressPath = "vellum/progress.txt";
  static constexpr const char* kCachePath = "vellum/library.txt";
  static constexpr const char* kBookmarksPath = "vellum/bookmarks.txt";
  static constexpr int kMaxBookmarks = 256;
  // Folders read below the root and below books/ ("books/Tolkien/Hobbit"
  // is two down), and how many folders at most.
  static constexpr int kMaxFolderDepth = 2;
  static constexpr int kMaxFolders = 64;
  // Where finished books go (Settings › Finished books).
  static constexpr const char* kReadFolder = "books/Read";

  Library();
  ~Library();
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  // Rebuilds the list: built-in samples, then the card's books (opened with
  // `epub` or `text` only when the cache does not know them; other formats
  // are left out without `text`), sorted by title. Reading positions are
  // reloaded from the card.
  void load(std::span<const Book* const> builtins, Storage* storage, Epub& epub, TextBook* text = nullptr);
  // Writes reading positions to the card.
  bool save_progress(Storage& storage) const;

  // How deep a folder is: 0 for the root (".") and books/, 1 for a folder
  // in either, and so on.
  static int folder_depth(const char* dir);
  // Whether books and pictures in this folder are shown: not hidden, not
  // Vellum's own (vellum, fonts, dictionaries) and not too deep.
  static bool shown_folder(const char* dir);
  // Folders the last load() read besides books/ and the root.
  int folder_count() const { return folder_count_; }
  // The book with this card path, or -1.
  int find(const char* path) const;
  // Moves book i's file into `folder` (named "Name (2).epub" when the name
  // is taken) with its cover thumbnails, cache line, reading position and
  // bookmarks. Shelves, highlights and reading stats are the caller's.
  // Returns false when the file could not be moved.
  bool move_book(Storage& storage, int i, const char* folder);

  int count() const { return count_; }
  const LibraryEntry& entry(int i) const { return entries_[i]; }
  BookState& state(int i) { return entries_[i].state; }
  const BookState& state(int i) const { return entries_[i].state; }

  // Writes the indices of book i's bookmarks to `out` in reading order.
  // Returns how many were written.
  int bookmarks_of(int book, int* out, int max) const;
  const Bookmark& bookmark(int k) const { return bookmarks_[k]; }
  // Returns false when there is no room for another bookmark.
  bool add_bookmark(int book, BookPos position, uint8_t percent, const char* snippet);
  void remove_bookmark(int k);
  bool save_bookmarks(Storage& storage) const;

  // Records that book i was opened just now.
  void touch(int i);
  // The book read most recently, or the first book when none has been opened.
  int current() const;
  // Writes book indices to `out`, most recently opened first and unopened
  // books after them in library order. Returns how many were written.
  int by_recent(int* out, int max) const;
  // Writes book indices to `out` in the given order: by_recent(); by title
  // (library order); by the author's surname, then title; or most recently
  // added first (the samples last, by title). Returns how many were written.
  int sorted(LibrarySort order, int* out, int max) const;

 private:
  void scan_folder(Storage& storage, const char* dir, Epub& epub, TextBook* text);
  void load_cache(Storage& storage);
  void save_cache(Storage& storage) const;
  void load_progress(Storage& storage);
  void load_bookmarks(Storage& storage);

  LibraryEntry* entries_;
  int count_ = 0;
  LibraryEntry* cache_;  // details read from library.txt
  int cache_count_ = 0;
  bool cache_dirty_ = false;
  // Folders still to read during load(), breadth first.
  char (*folders_)[sizeof(LibraryEntry::path)];
  int folders_queued_ = 0;
  int folder_count_ = 0;
  uint32_t clock_ = 0;
  Bookmark* bookmarks_;
  int bookmark_count_ = 0;
};

// Search: text compared without case or accents, so "zemaite" finds
// "Žemaitė". Latin letters (Latin-1 and Latin Extended-A) lose their marks;
// other characters are kept as they are.
//
// Writes the folded form of `in` to out (NUL-terminated, cut to fit at a
// whole character). Returns its length.
size_t fold_for_search(std::string_view in, char* out, size_t size);
// Whether `query` (any case and accents) appears in `text`. Spaces at the
// ends of the query are ignored; an empty query matches everything.
bool matches_search(std::string_view text, std::string_view query);

}  // namespace vellum

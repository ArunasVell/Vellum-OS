// Shelves: collections the reader makes of their books ("Summer",
// "Poetry"), kept in vellum/shelves.txt on the card.
//
// The file lists each shelf's name on a line of its own, then its books
// one per line, each a tab and the book's card path (LibraryEntry::path):
//
//   Poetry
//   <tab>books/Maironis - Pavasario balsai.epub
//   Summer
//
// A book can be on several shelves. Books that leave the card stay on their
// shelves (they come back when the file does).
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/hal.h"
#include "vellum/library.h"

namespace vellum {

class Shelves {
 public:
  static constexpr int kMax = 16;        // shelves
  static constexpr int kMaxBooks = 256;  // books on shelves, counted once per shelf
  static constexpr int kNameBytes = 40;  // a name, UTF-8, with its NUL
  static constexpr const char* kPath = "vellum/shelves.txt";

  Shelves();
  ~Shelves();
  Shelves(const Shelves&) = delete;
  Shelves& operator=(const Shelves&) = delete;

  // Reads the card's shelves (none when the file is missing).
  void load(Storage& storage);
  bool save(Storage& storage) const;

  int count() const { return count_; }
  const char* name(int shelf) const { return names_[shelf]; }
  // Adds an empty shelf at the end; returns its index, or -1 when there are
  // kMax already or the name is empty once trimmed. Names are trimmed and
  // cut to fit.
  int add(std::string_view name);
  bool rename(int shelf, std::string_view name);
  void remove(int shelf);

  // Whether the book with this card path is on the shelf.
  bool contains(int shelf, const char* path) const;
  // Puts a book on a shelf or takes it off. Returns false when there is no
  // room for another book.
  bool set(int shelf, const char* path, bool on);
  // How many of the library's books are on the shelf.
  int books_on(int shelf, const Library& library) const;

 private:
  struct Entry {
    uint8_t shelf;
    char path[sizeof(LibraryEntry::path)];
  };
  char names_[kMax][kNameBytes] = {};
  int count_ = 0;
  Entry* entries_;  // kMaxBooks, allocated once
  int entry_count_ = 0;
};

}  // namespace vellum

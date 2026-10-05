// The free book store's pieces that are not screens: which catalogues it
// offers, fetching a list page, naming and downloading a book to the card.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"
#include "vellum/opds.h"
#include "vellum/strings.h"

namespace vellum::store {

// A list in a catalogue. `name` indexes Strings::store_lists.
struct List {
  uint8_t name;
  const char* url;
};

// A catalogue of free books. `about` indexes Strings::store_abouts.
struct Source {
  const char* name;  // its own name, the same in every language
  uint8_t about;
  const List* lists;
  int list_count;
};

int source_count();
const Source& source(int i);

// Project Gutenberg's search feed for `words` (any language), for a search
// row once the on-screen keyboard exists. Returns false when it does not fit.
bool search_url(const char* words, char* out, size_t size);

// Fetches one page of a list into feed (its entries are the caller's).
// NoServer also covers an answer that was not a catalogue.
NetResult fetch_feed(Network& net, const char* url, OpdsFeed& feed, OpdsParser& parser);

// A free name for the book in books/: "Author - Title.epub", the title
// without its subtitle, made safe for the card, with " 2", " 3"... when
// that name is taken. Returns false when no name can be made.
bool book_path(Storage& storage, const char* title, const char* author, char* out, size_t size);

enum class Saved : uint8_t {
  Ok,
  NoNetwork,
  NoWifi,
  NoServer,  // could not reach it, or it broke off
  NotBook,   // the answer was not an EPUB (a web page, say); nothing kept
  CardFull,  // the card would not take it all; nothing kept
};

// Downloads `url` into `path`; nothing is left on the card unless the whole
// book arrived. progress(ctx, percent) is called as it comes (only when the
// server gives the length).
using ProgressFn = void (*)(void* ctx, int percent);
Saved download(Network& net, Storage& storage, const char* url, const char* path, ProgressFn progress, void* ctx);

// "1.2 MB" or "640 KB", with the language's decimal mark.
void format_size(Language lang, uint32_t bytes, char* out, size_t size);

}  // namespace vellum::store

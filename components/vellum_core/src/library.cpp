#include "vellum/library.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/textfile.h"

namespace vellum {
namespace {

constexpr uint32_t kFileBytes = 128 * 1024;  // progress and cache files
constexpr const char* kBuiltinPrefix = "builtin:";

bool ends_with_epub(const char* name) {
  const size_t n = std::strlen(name);
  if (n < 5) return false;
  const char* ext = name + n - 5;
  return (ext[0] == '.') && (ext[1] | 32) == 'e' && (ext[2] | 32) == 'p' && (ext[3] | 32) == 'u' &&
         (ext[4] | 32) == 'b';
}

char fold(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Title order, ignoring case and a leading "The ".
bool title_less(const LibraryEntry& a, const LibraryEntry& b) {
  const char* x = a.title;
  const char* y = b.title;
  if (std::strncmp(x, "The ", 4) == 0) x += 4;
  if (std::strncmp(y, "The ", 4) == 0) y += 4;
  for (; *x && *y; ++x, ++y) {
    if (fold(*x) != fold(*y)) return fold(*x) < fold(*y);
  }
  return *y != '\0';
}

// Appends a field to a line, with tabs and newlines turned into spaces.
size_t put(char* out, size_t pos, size_t cap, const char* text, char end) {
  for (const char* p = text; *p && pos + 2 < cap; ++p) out[pos++] = (*p == '\t' || *p == '\n' || *p == '\r') ? ' ' : *p;
  if (pos + 1 < cap) out[pos++] = end;
  return pos;
}

size_t put_number(char* out, size_t pos, size_t cap, uint32_t v, char end) {
  char num[12];
  std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(v));
  return put(out, pos, cap, num, end);
}

char* file_buffer() {
  static char* buf = new (std::nothrow) char[kFileBytes];
  return buf;
}

}  // namespace

Library::Library()
    : entries_(new(std::nothrow) LibraryEntry[kMaxBooks]),
      cache_(new(std::nothrow) LibraryEntry[kMaxBooks]),
      bookmarks_(new(std::nothrow) Bookmark[kMaxBookmarks]) {}

Library::~Library() {
  delete[] entries_;
  delete[] cache_;
  delete[] bookmarks_;
}

void Library::load(std::span<const Book* const> builtins, Storage* storage, Epub& epub) {
  count_ = 0;
  if (entries_ == nullptr) return;
  for (const Book* b : builtins) {
    if (count_ >= kMaxBooks) break;
    LibraryEntry& e = entries_[count_++];
    e = {};
    e.builtin = b;
    std::snprintf(e.path, sizeof(e.path), "%s%s", kBuiltinPrefix, b->title);
    std::snprintf(e.title, sizeof(e.title), "%s", b->title);
    std::snprintf(e.author, sizeof(e.author), "%s", b->author);
    std::snprintf(e.language, sizeof(e.language), "%s", b->language);
  }
  if (storage != nullptr && cache_ != nullptr) {
    load_cache(*storage);
    cache_dirty_ = false;
    scan_folder(*storage, "books", epub);
    scan_folder(*storage, ".", epub);
    if (cache_dirty_) {
      // Rewrite the cache from what is on the card now.
      char* buf = file_buffer();
      size_t n = 0;
      for (int i = 0; buf != nullptr && i < count_; ++i) {
        const LibraryEntry& e = entries_[i];
        if (e.builtin != nullptr) continue;
        n = put_number(buf, n, kFileBytes, e.file_size, '\t');
        n = put(buf, n, kFileBytes, e.path, '\t');
        n = put(buf, n, kFileBytes, e.title, '\t');
        n = put(buf, n, kFileBytes, e.author, '\t');
        n = put(buf, n, kFileBytes, e.language, '\n');
      }
      if (buf != nullptr) storage->write(kCachePath, buf, static_cast<uint32_t>(n));
    }
  }
  std::stable_sort(entries_, entries_ + count_, title_less);
  clock_ = 0;
  bookmark_count_ = 0;
  if (storage != nullptr) {
    load_progress(*storage);
    load_bookmarks(*storage);
  }
}

void Library::load_cache(Storage& storage) {
  cache_count_ = 0;
  char* buf = file_buffer();
  if (buf == nullptr) return;
  const int32_t n = textfile::read(storage, kCachePath, buf, kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(buf, static_cast<size_t>(n)), [&](const std::string_view* f, int count) {
    if (count < 5 || cache_count_ >= kMaxBooks) return;
    LibraryEntry& e = cache_[cache_count_++];
    e = {};
    e.file_size = textfile::to_uint(f[0]);
    textfile::copy(f[1], e.path, sizeof(e.path));
    textfile::copy(f[2], e.title, sizeof(e.title));
    textfile::copy(f[3], e.author, sizeof(e.author));
    textfile::copy(f[4], e.language, sizeof(e.language));
  });
}

void Library::scan_folder(Storage& storage, const char* dir, Epub& epub) {
  struct Found {
    char name[128];
    uint32_t size;
  };
  // Collect names first: opening books while listing would reuse the
  // storage's path buffer.
  static Found* found = new (std::nothrow) Found[kMaxBooks];
  if (found == nullptr) return;
  struct Ctx {
    Found* found;
    int n;
  } ctx{found, 0};
  storage.list(
      dir,
      [](void* p, const char* name, bool is_dir, uint32_t size) {
        auto* c = static_cast<Ctx*>(p);
        if (is_dir || !ends_with_epub(name) || c->n >= kMaxBooks || std::strlen(name) >= sizeof(Found::name)) return;
        std::snprintf(c->found[c->n].name, sizeof(Found::name), "%s", name);
        c->found[c->n++].size = size;
      },
      &ctx);

  for (int k = 0; k < ctx.n && count_ < kMaxBooks; ++k) {
    LibraryEntry& e = entries_[count_];
    e = {};
    if (std::strcmp(dir, ".") == 0) {
      std::snprintf(e.path, sizeof(e.path), "%s", found[k].name);
    } else {
      std::snprintf(e.path, sizeof(e.path), "%s/%s", dir, found[k].name);
    }
    e.file_size = found[k].size;
    bool cached = false;
    for (int c = 0; c < cache_count_; ++c) {
      if (cache_[c].file_size == e.file_size && std::strcmp(cache_[c].path, e.path) == 0) {
        std::memcpy(e.title, cache_[c].title, sizeof(e.title));
        std::memcpy(e.author, cache_[c].author, sizeof(e.author));
        std::memcpy(e.language, cache_[c].language, sizeof(e.language));
        cached = true;
        break;
      }
    }
    if (!cached) {
      if (!epub.open(storage, e.path)) continue;  // not a readable EPUB: leave it out
      std::memcpy(e.title, epub.info().title, sizeof(e.title));
      std::memcpy(e.author, epub.info().author, sizeof(e.author));
      std::memcpy(e.language, epub.info().language, sizeof(e.language));
      epub.close();
    }
    cache_dirty_ = cache_dirty_ || !cached;
    ++count_;
  }
  // A book that left the card also makes the cache stale.
  if (cache_count_ > 0 && std::strcmp(dir, ".") == 0) {
    int on_card = 0;
    for (int i = 0; i < count_; ++i) on_card += entries_[i].builtin == nullptr;
    if (on_card != cache_count_) cache_dirty_ = true;
  }
}

void Library::load_progress(Storage& storage) {
  char* buf = file_buffer();
  if (buf == nullptr) return;
  const int32_t n = textfile::read(storage, kProgressPath, buf, kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(buf, static_cast<size_t>(n)), [&](const std::string_view* f, int count) {
    if (count < 6) return;
    for (int i = 0; i < count_; ++i) {
      if (f[0] != entries_[i].path) continue;
      BookState& s = entries_[i].state;
      s.position.chapter = static_cast<uint16_t>(textfile::to_uint(f[1]));
      s.position.text = {textfile::to_uint(f[2]), textfile::to_uint(f[3])};
      s.percent = static_cast<uint8_t>(std::min<uint32_t>(textfile::to_uint(f[4]), 100));
      s.last_opened = textfile::to_uint(f[5]);
      clock_ = std::max(clock_, s.last_opened);
      break;
    }
  });
}

bool Library::save_progress(Storage& storage) const {
  char* buf = file_buffer();
  if (buf == nullptr) return false;
  size_t n = 0;
  for (int i = 0; i < count_; ++i) {
    const LibraryEntry& e = entries_[i];
    if (!e.state.started()) continue;
    n = put(buf, n, kFileBytes, e.path, '\t');
    n = put_number(buf, n, kFileBytes, e.state.position.chapter, '\t');
    n = put_number(buf, n, kFileBytes, e.state.position.text.paragraph, '\t');
    n = put_number(buf, n, kFileBytes, e.state.position.text.offset, '\t');
    n = put_number(buf, n, kFileBytes, e.state.percent, '\t');
    n = put_number(buf, n, kFileBytes, e.state.last_opened, '\n');
  }
  return storage.write(kProgressPath, buf, static_cast<uint32_t>(n));
}

void Library::load_bookmarks(Storage& storage) {
  char* buf = file_buffer();
  if (buf == nullptr || bookmarks_ == nullptr) return;
  const int32_t n = textfile::read(storage, kBookmarksPath, buf, kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(buf, static_cast<size_t>(n)), [&](const std::string_view* f, int count) {
    if (count < 5 || bookmark_count_ >= kMaxBookmarks) return;
    Bookmark& b = bookmarks_[bookmark_count_++];
    b = {};
    textfile::copy(f[0], b.path, sizeof(b.path));
    b.position.chapter = static_cast<uint16_t>(textfile::to_uint(f[1]));
    b.position.text = {textfile::to_uint(f[2]), textfile::to_uint(f[3])};
    b.percent = static_cast<uint8_t>(std::min<uint32_t>(textfile::to_uint(f[4]), 100));
    if (count > 5) textfile::copy(f[5], b.snippet, sizeof(b.snippet));
  });
}

bool Library::save_bookmarks(Storage& storage) const {
  char* buf = file_buffer();
  if (buf == nullptr || bookmarks_ == nullptr) return false;
  size_t n = 0;
  for (int k = 0; k < bookmark_count_; ++k) {
    const Bookmark& b = bookmarks_[k];
    n = put(buf, n, kFileBytes, b.path, '\t');
    n = put_number(buf, n, kFileBytes, b.position.chapter, '\t');
    n = put_number(buf, n, kFileBytes, b.position.text.paragraph, '\t');
    n = put_number(buf, n, kFileBytes, b.position.text.offset, '\t');
    n = put_number(buf, n, kFileBytes, b.percent, '\t');
    n = put(buf, n, kFileBytes, b.snippet, '\n');
  }
  return storage.write(kBookmarksPath, buf, static_cast<uint32_t>(n));
}

int Library::bookmarks_of(int book, int* out, int max) const {
  int n = 0;
  for (int k = 0; k < bookmark_count_ && n < max; ++k) {
    if (std::strcmp(bookmarks_[k].path, entries_[book].path) == 0) out[n++] = k;
  }
  std::sort(out, out + n, [&](int a, int b) {
    const BookPos& x = bookmarks_[a].position;
    const BookPos& y = bookmarks_[b].position;
    if (x.chapter != y.chapter) return x.chapter < y.chapter;
    if (x.text.paragraph != y.text.paragraph) return x.text.paragraph < y.text.paragraph;
    return x.text.offset < y.text.offset;
  });
  return n;
}

bool Library::add_bookmark(int book, BookPos position, uint8_t percent, const char* snippet) {
  if (bookmarks_ == nullptr || bookmark_count_ >= kMaxBookmarks) return false;
  Bookmark& b = bookmarks_[bookmark_count_++];
  b = {};
  std::snprintf(b.path, sizeof(b.path), "%s", entries_[book].path);
  b.position = position;
  b.percent = percent;
  std::snprintf(b.snippet, sizeof(b.snippet), "%s", snippet);
  return true;
}

void Library::remove_bookmark(int k) {
  if (k < 0 || k >= bookmark_count_) return;
  for (int i = k + 1; i < bookmark_count_; ++i) bookmarks_[i - 1] = bookmarks_[i];
  --bookmark_count_;
}

void Library::touch(int i) { entries_[i].state.last_opened = ++clock_; }

int Library::current() const {
  int best = 0;
  for (int i = 1; i < count_; ++i) {
    if (entries_[i].state.last_opened > entries_[best].state.last_opened) best = i;
  }
  return best;
}

int Library::by_recent(int* out, int max) const {
  const int n = std::min(max, count_);
  int order[kMaxBooks];
  for (int i = 0; i < count_; ++i) order[i] = i;
  // Stable, so unopened books (all 0) keep library order.
  std::stable_sort(order, order + count_,
                   [&](int a, int b) { return entries_[a].state.last_opened > entries_[b].state.last_opened; });
  std::copy_n(order, n, out);
  return n;
}

}  // namespace vellum

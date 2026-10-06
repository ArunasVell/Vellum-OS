#include "vellum/library.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/covers.h"
#include "vellum/text.h"
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

// ASCII letters in any case: the card's file system does not tell them apart.
bool same_name(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
    const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
    if (x != y) return false;
  }
  return true;
}

bool ends_with(std::string_view name, std::string_view ext) {
  return name.size() > ext.size() && same_name(name.substr(name.size() - ext.size()), ext);
}

// Base letters of Latin-1 (from U+00C0) and Latin Extended-A (U+0100 to
// U+017F), lowercase; '*' keeps the character (× and ÷).
constexpr char kLatin1[] = "aaaaaaaceeeeiiiidnooooo*ouuuuytsaaaaaaaceeeeiiiidnooooo*ouuuuyty";
constexpr char kLatinA[] =
    "aaaaaacccccccc"
    "dddd"
    "eeeeeeeeee"
    "gggggggghhhh"
    "iiiiiiiiiiiijjkkk"
    "llllllllllnnnnnnnnn"
    "oooooooorrrrrrssssssss"
    "ttttttuuuuuuuuuuuuwwyyyzzzzzzs";
static_assert(sizeof(kLatin1) == 0x100 - 0xC0 + 1 && sizeof(kLatinA) == 0x180 - 0x100 + 1);

// The letter a character is searched as: lowercase, without its marks.
uint32_t fold_char(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if (cp >= 0xC0 && cp < 0x100 && kLatin1[cp - 0xC0] != '*') return static_cast<uint8_t>(kLatin1[cp - 0xC0]);
  if (cp >= 0x100 && cp < 0x180) return static_cast<uint8_t>(kLatinA[cp - 0x100]);
  return cp;
}

size_t put_utf8(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

// Folded text for sorting: 128 bytes are plenty to tell titles apart.
struct SortKey {
  char text[128];
  explicit SortKey(std::string_view s) { fold_for_search(s, text, sizeof(text)); }
};

// Compares folded text; ties (only accents or case apart) by the bytes, so
// "c" comes before "č".
int compare_folded(std::string_view x, std::string_view y) {
  const int c = std::strcmp(SortKey(x).text, SortKey(y).text);
  return c != 0 ? c : x.compare(y);
}

std::string_view without_the(std::string_view title) {
  if (title.starts_with("The ")) title.remove_prefix(4);
  return title;
}

// Title order, ignoring case, accents and a leading "The ".
bool title_less(const LibraryEntry& a, const LibraryEntry& b) {
  return compare_folded(without_the(a.title), without_the(b.title)) < 0;
}

// The surname in an author's name: before a comma ("Carroll, Lewis"), else
// the last word ("Lewis Carroll").
std::string_view surname(std::string_view author) {
  if (const size_t comma = author.find(','); comma != std::string_view::npos) return author.substr(0, comma);
  while (!author.empty() && author.back() == ' ') author.remove_suffix(1);
  const size_t space = author.rfind(' ');
  return space == std::string_view::npos ? author : author.substr(space + 1);
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

bool is_book_file(const char* name) {
  return ends_with_epub(name) || TextBook::format_of(name) != TextBook::Format::None;
}

bool is_picture_file(const char* name) {
  return ends_with(name, ".jpg") || ends_with(name, ".jpeg") || ends_with(name, ".png") || ends_with(name, ".bmp");
}

Library::Library()
    : entries_(new(std::nothrow) LibraryEntry[kMaxBooks]),
      cache_(new(std::nothrow) LibraryEntry[kMaxBooks]),
      folders_(new(std::nothrow) char[kMaxFolders][sizeof(LibraryEntry::path)]),
      bookmarks_(new(std::nothrow) Bookmark[kMaxBookmarks]) {}

Library::~Library() {
  delete[] entries_;
  delete[] cache_;
  delete[] folders_;
  delete[] bookmarks_;
}

void Library::load(std::span<const Book* const> builtins, Storage* storage, Epub& epub, TextBook* text) {
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
    folder_count_ = 0;
    if (folders_ == nullptr) {
      scan_folder(*storage, "books", epub, text);
      scan_folder(*storage, ".", epub, text);
    } else {
      // books/ and the root, then the folders found in them, breadth first.
      std::snprintf(folders_[0], sizeof(folders_[0]), "books");
      std::snprintf(folders_[1], sizeof(folders_[1]), ".");
      folders_queued_ = 2;
      for (int k = 0; k < folders_queued_; ++k) {
        folder_count_ += k >= 2 ? 1 : 0;
        scan_folder(*storage, folders_[k], epub, text);
      }
    }
    // A book that left the card also makes the cache stale.
    int on_card = 0;
    for (int i = 0; i < count_; ++i) on_card += entries_[i].builtin == nullptr;
    if (cache_count_ > 0 && on_card != cache_count_) cache_dirty_ = true;
    // Books not seen before are numbered on from the newest, in the order found.
    uint32_t newest = 0;
    for (int i = 0; i < count_; ++i) newest = std::max(newest, entries_[i].added);
    for (int i = 0; i < count_; ++i) {
      if (entries_[i].builtin != nullptr || entries_[i].added != 0) continue;
      entries_[i].added = ++newest;
      cache_dirty_ = true;
    }
    if (cache_dirty_) save_cache(*storage);
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
    // "2": a cover made at the size the full-screen sleep cover wants. Older
    // caches, and "1" (smaller thumbnails), look again.
    e.cover = count > 5 && f[5] == "2" ? 1 : count > 5 && f[5] == "0" ? 0 : -1;
    e.added = count > 6 ? textfile::to_uint(f[6]) : 0;  // older caches number their books again
  });
}

void Library::save_cache(Storage& storage) const {
  // Rewritten whole from what is on the card now.
  char* buf = file_buffer();
  if (buf == nullptr) return;
  size_t n = 0;
  for (int i = 0; i < count_; ++i) {
    const LibraryEntry& e = entries_[i];
    if (e.builtin != nullptr) continue;
    n = put_number(buf, n, kFileBytes, e.file_size, '\t');
    n = put(buf, n, kFileBytes, e.path, '\t');
    n = put(buf, n, kFileBytes, e.title, '\t');
    n = put(buf, n, kFileBytes, e.author, '\t');
    n = put(buf, n, kFileBytes, e.language, '\t');
    n = put(buf, n, kFileBytes, e.cover > 0 ? "2" : e.cover == 0 ? "0" : "-", '\t');  // "-": look again
    n = put_number(buf, n, kFileBytes, e.added, '\n');
  }
  storage.write(kCachePath, buf, static_cast<uint32_t>(n));
}

void Library::scan_folder(Storage& storage, const char* dir, Epub& epub, TextBook* text) {
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
    Library* library;
    const char* dir;
  } ctx{found, 0, this, dir};
  storage.list(
      dir,
      [](void* p, const char* name, bool is_dir, uint32_t size) {
        auto* c = static_cast<Ctx*>(p);
        if (is_dir) {
          // A folder to read later, unless it is too deep, Vellum's own or books/ again.
          Library& lib = *c->library;
          const bool root = std::strcmp(c->dir, ".") == 0;
          if (lib.folders_ == nullptr || lib.folders_queued_ >= kMaxFolders || (root && same_name(name, "books")))
            return;
          char* out = lib.folders_[lib.folders_queued_];
          const int len = root ? std::snprintf(out, sizeof(lib.folders_[0]), "%s", name)
                               : std::snprintf(out, sizeof(lib.folders_[0]), "%s/%s", c->dir, name);
          if (len > 0 && len < static_cast<int>(sizeof(Found::name)) && shown_folder(out)) ++lib.folders_queued_;
          return;
        }
        if (!is_book_file(name) || c->n >= kMaxBooks || std::strlen(name) >= sizeof(Found::name)) return;
        std::snprintf(c->found[c->n].name, sizeof(Found::name), "%s", name);
        c->found[c->n++].size = size;
      },
      &ctx);

  for (int k = 0; k < ctx.n && count_ < kMaxBooks; ++k) {
    LibraryEntry& e = entries_[count_];
    e = {};
    if (std::strcmp(dir, ".") == 0) {
      std::snprintf(e.path, sizeof(e.path), "%s", found[k].name);
    } else if (std::snprintf(e.path, sizeof(e.path), "%s/%s", dir, found[k].name) >= static_cast<int>(sizeof(e.path))) {
      continue;  // a path too long to keep
    }
    e.file_size = found[k].size;
    bool cached = false;
    for (int c = 0; c < cache_count_; ++c) {
      if (cache_[c].file_size == e.file_size && std::strcmp(cache_[c].path, e.path) == 0) {
        std::memcpy(e.title, cache_[c].title, sizeof(e.title));
        std::memcpy(e.author, cache_[c].author, sizeof(e.author));
        std::memcpy(e.language, cache_[c].language, sizeof(e.language));
        e.cover = cache_[c].cover;
        e.added = cache_[c].added;
        cached = true;
        break;
      }
    }
    if (!cached || e.cover < 0) {
      // Not a readable book: leave it out.
      const bool is_epub = ends_with_epub(e.path);
      if (is_epub ? !epub.open(storage, e.path) : text == nullptr || !text->open(storage, e.path)) continue;
      const EpubInfo& info = is_epub ? epub.info() : text->info();
      if (!cached) {
        std::memcpy(e.title, info.title, sizeof(e.title));
        std::memcpy(e.author, info.author, sizeof(e.author));
        std::memcpy(e.language, info.language, sizeof(e.language));
      }
      // The cover, once: decoded and shrunk now so screens only read thumbnails.
      const uint8_t* picture = nullptr;
      const int32_t len = is_epub ? epub.read_cover(&picture) : text->read_cover(&picture);
      char cover_path[64];
      covers::path_for(e.path, e.file_size, cover_path, sizeof(cover_path));
      e.cover = len > 0 && covers::make(storage, cover_path, picture, static_cast<uint32_t>(len)) ? 1 : 0;
      is_epub ? epub.close() : text->close();
      cache_dirty_ = true;
    }
    ++count_;
  }
}

int Library::folder_depth(const char* dir) {
  if (dir[0] == '\0' || std::strcmp(dir, ".") == 0) return 0;
  int depth = 1;
  for (const char* p = dir; *p; ++p) depth += *p == '/' ? 1 : 0;
  // books/ counts as the root.
  const bool in_books = std::strncmp(dir, "books", 5) == 0 && (dir[5] == '\0' || dir[5] == '/');
  return in_books ? depth - 1 : depth;
}

bool Library::shown_folder(const char* dir) {
  if (folder_depth(dir) > kMaxFolderDepth) return false;
  // Vellum's own folders and the card's system ones hold no books.
  const char* slash = std::strchr(dir, '/');
  const std::string_view top(dir, slash != nullptr ? static_cast<size_t>(slash - dir) : std::strlen(dir));
  for (const char* own : {"vellum", "fonts", "dictionaries", "System Volume Information", "LOST.DIR"}) {
    if (same_name(top, own)) return false;
  }
  // Nor do hidden ones.
  for (const char* p = dir; p != nullptr; p = std::strchr(p, '/')) {
    if (*p == '/') ++p;
    if (p[0] == '.' && p[1] != '\0') return false;
  }
  return true;
}

int Library::find(const char* path) const {
  for (int i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].path, path) == 0) return i;
  }
  return -1;
}

bool Library::move_book(Storage& storage, int i, const char* folder) {
  LibraryEntry& e = entries_[i];
  if (e.builtin != nullptr) return false;
  // The same name in the new folder, or "Name (2).epub" and on when it is taken.
  const char* slash = std::strrchr(e.path, '/');
  const char* name = slash != nullptr ? slash + 1 : e.path;
  const char* dot = std::strchr(name, '.');
  const int stem = dot != nullptr ? static_cast<int>(dot - name) : static_cast<int>(std::strlen(name));
  char to[sizeof(e.path)];
  bool moved = false;
  for (int n = 1; n < 10 && !moved; ++n) {
    const int len = n == 1 ? std::snprintf(to, sizeof(to), "%s/%s", folder, name)
                           : std::snprintf(to, sizeof(to), "%s/%.*s (%d)%s", folder, stem, name, n, name + stem);
    if (len <= 0 || len >= static_cast<int>(sizeof(to))) return false;
    if (std::strcmp(to, e.path) == 0) return false;  // already there
    moved = storage.rename(e.path, to);
  }
  if (!moved) return false;
  // The thumbnails are named after the path: they move too, or are made again.
  char old_cover[64], new_cover[64];
  covers::path_for(e.path, e.file_size, old_cover, sizeof(old_cover));
  covers::path_for(to, e.file_size, new_cover, sizeof(new_cover));
  if (e.cover > 0 && !storage.rename(old_cover, new_cover)) e.cover = -1;
  for (int k = 0; k < bookmark_count_; ++k) {
    if (std::strcmp(bookmarks_[k].path, e.path) == 0)
      std::snprintf(bookmarks_[k].path, sizeof(Bookmark::path), "%s", to);
  }
  for (int c = 0; c < cache_count_; ++c) {
    if (std::strcmp(cache_[c].path, e.path) == 0) std::snprintf(cache_[c].path, sizeof(cache_[c].path), "%s", to);
  }
  std::snprintf(e.path, sizeof(e.path), "%s", to);
  save_cache(storage);
  save_progress(storage);
  save_bookmarks(storage);
  return true;
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
      s.finished = count > 6 && f[6] == "1";  // older files have no such field
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
    if (!e.state.started() && !e.state.finished) continue;
    n = put(buf, n, kFileBytes, e.path, '\t');
    n = put_number(buf, n, kFileBytes, e.state.position.chapter, '\t');
    n = put_number(buf, n, kFileBytes, e.state.position.text.paragraph, '\t');
    n = put_number(buf, n, kFileBytes, e.state.position.text.offset, '\t');
    n = put_number(buf, n, kFileBytes, e.state.percent, '\t');
    n = put_number(buf, n, kFileBytes, e.state.last_opened, '\t');
    n = put(buf, n, kFileBytes, e.state.finished ? "1" : "0", '\n');
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

int Library::sorted(LibrarySort order, int* out, int max) const {
  if (order == LibrarySort::Recent) return by_recent(out, max);
  const int n = std::min(max, count_);
  int all[kMaxBooks];
  for (int i = 0; i < count_; ++i) all[i] = i;  // library order is title order
  if (order == LibrarySort::Author) {
    std::stable_sort(all, all + count_, [&](int a, int b) {
      const char* x = entries_[a].author;
      const char* y = entries_[b].author;
      if (const int c = compare_folded(surname(x), surname(y)); c != 0) return c < 0;
      return compare_folded(x, y) < 0;
    });
  } else if (order == LibrarySort::Added) {
    std::stable_sort(all, all + count_, [&](int a, int b) { return entries_[a].added > entries_[b].added; });
  }
  std::copy_n(all, n, out);
  return n;
}

size_t fold_for_search(std::string_view in, char* out, size_t size) {
  if (size == 0) return 0;
  size_t n = 0;
  size_t i = 0;
  while (i < in.size()) {
    char ch[4];
    const size_t len = put_utf8(fold_char(decode_utf8(in, i)), ch);
    if (n + len >= size) break;
    std::memcpy(out + n, ch, len);
    n += len;
  }
  out[n] = '\0';
  return n;
}

bool matches_search(std::string_view text, std::string_view query) {
  while (!query.empty() && query.front() == ' ') query.remove_prefix(1);
  while (!query.empty() && query.back() == ' ') query.remove_suffix(1);
  if (query.empty()) return true;
  char folded_text[512];
  char folded_query[256];
  const size_t t = fold_for_search(text, folded_text, sizeof(folded_text));
  const size_t q = fold_for_search(query, folded_query, sizeof(folded_query));
  return std::string_view(folded_text, t).find(std::string_view(folded_query, q)) != std::string_view::npos;
}

}  // namespace vellum

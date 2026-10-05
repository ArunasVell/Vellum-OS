#include "vellum/shelves.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/textfile.h"

namespace vellum {
namespace {

constexpr uint32_t kFileBytes = 48 * 1024;  // every shelf full of long paths still fits

// One buffer for reading and writing the file, made the first time.
char* file_buffer() {
  static char* buf = new (std::nothrow) char[kFileBytes];
  return buf;
}

// Copies a name without spaces at its ends, tabs or line breaks, cut to fit
// at a whole character. Returns its length.
size_t clean_name(std::string_view in, char* out, size_t size) {
  while (!in.empty() && (in.front() == ' ' || in.front() == '\t')) in.remove_prefix(1);
  while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n')) {
    in.remove_suffix(1);
  }
  size_t n = std::min(in.size(), size - 1);
  while (n > 0 && n < in.size() && (static_cast<uint8_t>(in[n]) & 0xC0) == 0x80) --n;
  for (size_t i = 0; i < n; ++i) out[i] = (in[i] == '\t' || in[i] == '\n' || in[i] == '\r') ? ' ' : in[i];
  out[n] = '\0';
  return n;
}

}  // namespace

Shelves::Shelves() : entries_(new(std::nothrow) Entry[kMaxBooks]) {}

Shelves::~Shelves() { delete[] entries_; }

void Shelves::load(Storage& storage) {
  count_ = 0;
  entry_count_ = 0;
  char* buf = file_buffer();
  if (buf == nullptr) return;
  const int32_t n = textfile::read(storage, kPath, buf, kFileBytes);
  if (n <= 0) return;
  int shelf = -1;
  textfile::for_each_line(std::string_view(buf, static_cast<size_t>(n)), [&](const std::string_view* f, int fields) {
    if (!f[0].empty()) {
      shelf = add(f[0]);  // a shelf
    } else if (fields > 1 && shelf >= 0 && !f[1].empty()) {
      char path[sizeof(Entry::path)];
      textfile::copy(f[1], path, sizeof(path));
      set(shelf, path, true);  // a book on the shelf above it
    }
  });
}

bool Shelves::save(Storage& storage) const {
  char* buf = file_buffer();
  if (buf == nullptr) return false;
  size_t n = 0;
  for (int s = 0; s < count_; ++s) {
    const int w = std::snprintf(buf + n, kFileBytes - n, "%s\n", names_[s]);
    if (w < 0 || n + static_cast<size_t>(w) >= kFileBytes) return false;
    n += static_cast<size_t>(w);
    for (int k = 0; entries_ != nullptr && k < entry_count_; ++k) {
      if (entries_[k].shelf != s) continue;
      const int len = std::snprintf(buf + n, kFileBytes - n, "\t%s\n", entries_[k].path);
      if (len < 0 || n + static_cast<size_t>(len) >= kFileBytes) return false;
      n += static_cast<size_t>(len);
    }
  }
  return storage.write(kPath, buf, static_cast<uint32_t>(n));
}

int Shelves::add(std::string_view name) {
  if (count_ >= kMax) return -1;
  if (clean_name(name, names_[count_], kNameBytes) == 0) return -1;
  return count_++;
}

int Shelves::find(std::string_view name) const {
  // Compared as the library's search compares: without case or accents.
  char cleaned[kNameBytes];
  char wanted[kNameBytes];
  char other[kNameBytes];
  clean_name(name, cleaned, sizeof(cleaned));
  fold_for_search(cleaned, wanted, sizeof(wanted));
  for (int s = 0; s < count_; ++s) {
    fold_for_search(names_[s], other, sizeof(other));
    if (std::strcmp(wanted, other) == 0) return s;
  }
  return -1;
}

bool Shelves::rename(int shelf, std::string_view name) {
  if (shelf < 0 || shelf >= count_) return false;
  char cleaned[kNameBytes];
  if (clean_name(name, cleaned, sizeof(cleaned)) == 0) return false;
  std::memcpy(names_[shelf], cleaned, sizeof(cleaned));
  return true;
}

void Shelves::remove(int shelf) {
  if (shelf < 0 || shelf >= count_) return;
  for (int s = shelf + 1; s < count_; ++s) std::memcpy(names_[s - 1], names_[s], kNameBytes);
  --count_;
  // Its books go; the books of later shelves move down with them.
  int kept = 0;
  for (int k = 0; k < entry_count_; ++k) {
    if (entries_[k].shelf == shelf) continue;
    entries_[kept] = entries_[k];
    if (entries_[kept].shelf > shelf) --entries_[kept].shelf;
    ++kept;
  }
  entry_count_ = kept;
}

bool Shelves::contains(int shelf, const char* path) const {
  for (int k = 0; k < entry_count_; ++k) {
    if (entries_[k].shelf == shelf && std::strcmp(entries_[k].path, path) == 0) return true;
  }
  return false;
}

bool Shelves::set(int shelf, const char* path, bool on) {
  if (shelf < 0 || shelf >= count_ || entries_ == nullptr) return false;
  for (int k = 0; k < entry_count_; ++k) {
    if (entries_[k].shelf != shelf || std::strcmp(entries_[k].path, path) != 0) continue;
    if (!on) {
      for (int j = k + 1; j < entry_count_; ++j) entries_[j - 1] = entries_[j];
      --entry_count_;
    }
    return true;
  }
  if (!on) return true;
  if (entry_count_ >= kMaxBooks) return false;
  Entry& e = entries_[entry_count_++];
  e.shelf = static_cast<uint8_t>(shelf);
  std::snprintf(e.path, sizeof(e.path), "%s", path);
  return true;
}

int Shelves::books_on(int shelf, const Library& library) const {
  int n = 0;
  for (int i = 0; i < library.count(); ++i) n += contains(shelf, library.entry(i).path);
  return n;
}

}  // namespace vellum

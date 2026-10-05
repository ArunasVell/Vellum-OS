#include "vellum/sleep_pictures.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#include "vellum/covers.h"
#include "vellum/textfile.h"

namespace vellum {
namespace {

constexpr uint32_t kStateBytes = 8 * 1024;          // "mark\tname" lines for every picture, and "last"
constexpr char kBadMark[4] = {'V', 'S', 'P', '0'};  // left in place of a picture that cannot be decoded

bool ends_with(const char* s, const char* ext) {
  const size_t n = std::strlen(s), e = std::strlen(ext);
  if (n <= e) return false;
  for (size_t i = 0; i < e; ++i) {
    char c = s[n - e + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != ext[i]) return false;
  }
  return true;
}

void cache_name(const char* name, uint32_t size, char* out, size_t out_size) {
  uint32_t h = 2166136261u;  // FNV-1a over the name and size
  for (const char* p = name; *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  for (int i = 0; i < 4; ++i) h = (h ^ ((size >> (8 * i)) & 0xFF)) * 16777619u;
  std::snprintf(out, out_size, "%08lx.pic", static_cast<unsigned long>(h));
}

}  // namespace

bool is_picture_file(const char* name) {
  if (name[0] == '.') return false;
  return ends_with(name, ".jpg") || ends_with(name, ".jpeg") || ends_with(name, ".png") || ends_with(name, ".bmp");
}

SleepPictures::SleepPictures()
    : pictures_(new(std::nothrow) Picture[kMax]), file_(new(std::nothrow) char[kStateBytes]) {}

SleepPictures::~SleepPictures() {
  delete[] pictures_;
  delete[] file_;
}

void SleepPictures::scan(Storage& storage) {
  count_ = 0;
  last_[0] = '\0';
  if (pictures_ == nullptr) return;
  storage.list(
      kFolder,
      [](void* ctx, const char* name, bool is_dir, uint32_t size) {
        auto* self = static_cast<SleepPictures*>(ctx);
        if (is_dir || size == 0 || self->count_ >= kMax || !is_picture_file(name)) return;
        if (std::strlen(name) >= sizeof(Picture::name)) return;
        Picture& p = self->pictures_[self->count_++];
        std::snprintf(p.name, sizeof(p.name), "%s", name);
        p.size = size;
        p.marked = false;
      },
      this);
  // By name, as the phone page and the picker list them.
  for (int i = 1; i < count_; ++i) {
    for (int k = i; k > 0 && std::strcmp(pictures_[k - 1].name, pictures_[k].name) > 0; --k) {
      std::swap(pictures_[k - 1], pictures_[k]);
    }
  }

  // Marks and the last picture shown, by name.
  if (file_ != nullptr) {
    const int32_t n = textfile::read(storage, kStatePath, file_, kStateBytes);
    if (n > 0) {
      textfile::for_each_line(std::string_view(file_, static_cast<size_t>(n)), [&](std::string_view* f, int fields) {
        if (fields < 2) return;
        if (f[0] == "last") textfile::copy(f[1], last_, sizeof(last_));
        if (f[0] != "mark") return;
        for (int i = 0; i < count_; ++i) {
          if (f[1] == pictures_[i].name) pictures_[i].marked = true;
        }
      });
    }
  }

  // Screen pictures no picture matches any more: deleted pictures, and
  // pictures replaced by a file of another size.
  struct Clean {
    SleepPictures* self;
    char stale[16][16];
    int count;
  } clean{this, {}, 0};
  storage.list(
      kCacheFolder,
      [](void* ctx, const char* name, bool is_dir, uint32_t) {
        auto* c = static_cast<Clean*>(ctx);
        if (is_dir || !ends_with(name, ".pic") || std::strlen(name) >= sizeof(c->stale[0]) || c->count >= 16) return;
        char want[16];
        for (int i = 0; i < c->self->count_; ++i) {
          cache_name(c->self->pictures_[i].name, c->self->pictures_[i].size, want, sizeof(want));
          if (std::strcmp(want, name) == 0) return;
        }
        std::snprintf(c->stale[c->count++], sizeof(c->stale[0]), "%s", name);
      },
      &clean);
  for (int i = 0; i < clean.count; ++i) {  // not while listing the folder
    char path[64];
    std::snprintf(path, sizeof(path), "%s/%s", kCacheFolder, clean.stale[i]);
    storage.remove(path);
  }
}

int SleepPictures::marked_count() const {
  int n = 0;
  for (int i = 0; i < count_; ++i) n += pictures_[i].marked ? 1 : 0;
  return n;
}

void SleepPictures::clear_marks() {
  for (int i = 0; i < count_; ++i) pictures_[i].marked = false;
}

bool SleepPictures::save(Storage& storage) const {
  if (file_ == nullptr) return false;
  uint32_t n = 0;
  auto line = [&](const char* key, const char* value) {
    const int k = std::snprintf(file_ + n, kStateBytes - n, "%s\t%s\n", key, value);
    if (k > 0 && n + static_cast<uint32_t>(k) < kStateBytes) n += static_cast<uint32_t>(k);
  };
  if (last_[0] != '\0') line("last", last_);
  for (int i = 0; i < count_; ++i) {
    if (pictures_[i].marked) line("mark", pictures_[i].name);
  }
  return storage.write(kStatePath, file_, n);
}

void SleepPictures::cache_path(int i, char* out, size_t size) const {
  char name[16];
  cache_name(pictures_[i].name, pictures_[i].size, name, sizeof(name));
  std::snprintf(out, size, "%s/%s", kCacheFolder, name);
}

bool SleepPictures::ready(Storage& storage, int i) const {
  char path[64];
  cache_path(i, path, sizeof(path));
  File* f = storage.open(path);
  if (f != nullptr) storage.close(f);
  return f != nullptr;
}

bool SleepPictures::prepare(Storage& storage, int i) {
  char path[64];
  cache_path(i, path, sizeof(path));
  if (File* f = storage.open(path)) {
    const bool bad = f->size() <= sizeof(kBadMark);  // tried before, and it did not decode
    storage.close(f);
    return !bad;
  }
  char picture[sizeof(Picture::name) + 8];
  std::snprintf(picture, sizeof(picture), "%s/%s", kFolder, pictures_[i].name);
  if (covers::make_screen(storage, picture, path)) return true;
  storage.write(path, kBadMark, sizeof(kBadMark));
  return false;
}

int SleepPictures::choose(Storage& storage, SleepOrder order, uint32_t seed, bool remember) {
  if (count_ == 0) return -1;
  // The order to try them in: the pinned or shuffled one first, then the
  // rest, should it not decode.
  int tries[kMax];
  int n = 0;
  const bool marks = marked_count() > 0;
  if (order == SleepOrder::One) {
    int pinned = 0;
    for (int i = count_ - 1; i >= 0; --i) {
      if (pictures_[i].marked) pinned = i;
    }
    tries[n++] = pinned;
    for (int i = 0; i < count_; ++i) {
      if (i != pinned) tries[n++] = i;
    }
  } else {
    // Shuffle: among the marked pictures, the one shown last at the end.
    int last = -1;
    for (int i = 0; i < count_; ++i) {
      if (marks && !pictures_[i].marked) continue;
      if (std::strcmp(pictures_[i].name, last_) == 0) {
        last = i;
      } else {
        tries[n++] = i;
      }
    }
    // A random start, then on round from there.
    if (n > 1) {
      seed = (seed ^ (seed >> 16)) * 0x45d9f3bu;
      seed ^= seed >> 16;
      const int start = static_cast<int>(seed % static_cast<uint32_t>(n));
      int rotated[kMax];
      for (int k = 0; k < n; ++k) rotated[k] = tries[(start + k) % n];
      std::memcpy(tries, rotated, sizeof(int) * static_cast<size_t>(n));
    }
    if (last >= 0) tries[n++] = last;
  }
  for (int k = 0; k < n; ++k) {
    if (!prepare(storage, tries[k])) continue;
    if (remember && std::strcmp(last_, pictures_[tries[k]].name) != 0) {
      std::snprintf(last_, sizeof(last_), "%s", pictures_[tries[k]].name);
      save(storage);
    }
    return tries[k];
  }
  return -1;
}

}  // namespace vellum

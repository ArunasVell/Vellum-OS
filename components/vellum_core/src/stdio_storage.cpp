#include "vellum/stdio_storage.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>

namespace vellum {

StdioStorage::StdioStorage(const char* root) {
  std::snprintf(root_, sizeof(root_), "%s", root);
  full_[0] = '\0';
}

bool StdioStorage::full_path(const char* path) {
  const int n = std::snprintf(full_, sizeof(full_), "%s/%s", root_, path);
  return n > 0 && n < static_cast<int>(sizeof(full_));
}

File* StdioStorage::open(const char* path) {
  if (!full_path(path)) return nullptr;
  for (StdioFile& f : files_) {
    if (f.fp != nullptr) continue;
    f.fp = std::fopen(full_, "rb");
    if (f.fp == nullptr) return nullptr;
    struct stat st;
    f.size_ = fstat(fileno(f.fp), &st) == 0 ? static_cast<uint32_t>(st.st_size) : 0;
    f.pos_ = 0;
    return &f;
  }
  return nullptr;  // all slots in use
}

void StdioStorage::close(File* file) {
  for (StdioFile& f : files_) {
    if (&f == file && f.fp != nullptr) {
      std::fclose(f.fp);
      f.fp = nullptr;
    }
  }
}

int32_t StdioStorage::StdioFile::read_at(uint32_t offset, void* buf, uint32_t len) {
  if (offset != pos_ && std::fseek(fp, static_cast<long>(offset), SEEK_SET) != 0) return -1;
  const size_t n = std::fread(buf, 1, len, fp);
  pos_ = offset + static_cast<uint32_t>(n);
  if (n < len && std::ferror(fp)) {
    std::clearerr(fp);
    pos_ = UINT32_MAX;  // position unknown: seek next time
    return -1;
  }
  return static_cast<int32_t>(n);
}

bool StdioStorage::make_parents(char* full) {
  for (char* p = full + 1; *p; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(full, 0775);  // fails harmlessly when it exists
    *p = '/';
  }
  return true;
}

bool StdioStorage::write(const char* path, const void* data, uint32_t len) {
  if (!full_path(path)) return false;
  make_parents(full_);
  // Write a temporary file, then rename it over the old one, so a power cut
  // leaves either the old or the new contents.
  char tmp[sizeof(full_) + 4];
  std::snprintf(tmp, sizeof(tmp), "%s.tmp", full_);
  std::FILE* fp = std::fopen(tmp, "wb");
  if (fp == nullptr) return false;
  const bool ok = std::fwrite(data, 1, len, fp) == len;
  if (std::fclose(fp) != 0 || !ok) {
    std::remove(tmp);
    return false;
  }
  std::remove(full_);  // FAT cannot rename over an existing file
  return std::rename(tmp, full_) == 0;
}

Writer* StdioStorage::create(const char* path) {
  if (writer_.fp != nullptr || !full_path(path)) return nullptr;
  make_parents(full_);
  std::snprintf(writer_.path, sizeof(writer_.path), "%s", full_);
  char part[sizeof(writer_.path) + 8];
  std::snprintf(part, sizeof(part), "%s.part", writer_.path);
  writer_.fp = std::fopen(part, "wb");
  return writer_.fp != nullptr ? &writer_ : nullptr;
}

bool StdioStorage::finish(Writer* w, bool keep) {
  if (w != &writer_ || writer_.fp == nullptr) return false;
  char part[sizeof(writer_.path) + 8];
  std::snprintf(part, sizeof(part), "%s.part", writer_.path);
  const bool closed = std::fclose(writer_.fp) == 0;
  writer_.fp = nullptr;
  if (!keep || !closed) {
    std::remove(part);
    return false;
  }
  std::remove(writer_.path);  // FAT cannot rename over an existing file
  return std::rename(part, writer_.path) == 0;
}

bool StdioStorage::rename(const char* from, const char* to) {
  if (!full_path(to)) return false;
  char dest[sizeof(full_)];
  std::snprintf(dest, sizeof(dest), "%s", full_);
  struct stat st;
  if (stat(dest, &st) == 0) return false;  // never over another file
  if (!full_path(from)) return false;
  make_parents(dest);
  return std::rename(full_, dest) == 0;
}

bool StdioStorage::list(const char* dir, ListFn fn, void* ctx) {
  if (!full_path(dir)) return false;
  DIR* d = opendir(full_);
  if (d == nullptr) return false;
  const size_t base = std::strlen(full_);
  while (const dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;  // ".", ".." and hidden files
    if (base + 1 + std::strlen(e->d_name) >= sizeof(full_)) continue;
    full_[base] = '/';
    std::strcpy(full_ + base + 1, e->d_name);
    struct stat st;
    if (stat(full_, &st) == 0) fn(ctx, e->d_name, S_ISDIR(st.st_mode), static_cast<uint32_t>(st.st_size));
    full_[base] = '\0';
  }
  closedir(d);
  return true;
}

}  // namespace vellum

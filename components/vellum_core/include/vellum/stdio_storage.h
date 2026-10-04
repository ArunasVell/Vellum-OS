// Storage on a mounted file system through C stdio: the SD card once ESP-IDF
// has mounted it, a folder on the desktop simulator, and the browser
// simulator's in-memory file system.
#pragma once

#include <cstdio>

#include "vellum/hal.h"

namespace vellum {

class StdioStorage : public Storage {
 public:
  // `root` is the mount point or folder, without a trailing slash.
  explicit StdioStorage(const char* root);

  File* open(const char* path) override;
  void close(File* f) override;
  bool write(const char* path, const void* data, uint32_t len) override;
  bool list(const char* dir, ListFn fn, void* ctx) override;

 private:
  class StdioFile : public File {
   public:
    uint32_t size() const override { return size_; }
    int32_t read_at(uint32_t offset, void* buf, uint32_t len) override;

    std::FILE* fp = nullptr;
    uint32_t size_ = 0;
    uint32_t pos_ = 0;  // where the next read starts without seeking
  };

  // Joins root and path into full_. Returns false when it does not fit.
  bool full_path(const char* path);
  bool make_parents(char* full);

  char root_[256];
  char full_[512];
  StdioFile files_[kMaxOpen];
};

}  // namespace vellum

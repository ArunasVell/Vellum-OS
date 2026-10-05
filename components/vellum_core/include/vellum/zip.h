// Read-only ZIP archives (EPUB files are ZIPs): the central directory is read
// once, then single entries are extracted, stored or deflated, straight into
// a caller's buffer. No ZIP64, no encryption.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/hal.h"

namespace vellum {

class Zip {
 public:
  static constexpr int kMaxEntries = 2048;
  static constexpr uint32_t kNameBytes = 64 * 1024;

  Zip();  // allocates the directory and inflater once
  ~Zip();
  Zip(const Zip&) = delete;
  Zip& operator=(const Zip&) = delete;

  // Reads the archive's directory. `file` must stay open while the Zip is used.
  bool open(File* file);
  void close() {
    file_ = nullptr;
    count_ = 0;
  }

  int count() const { return count_; }
  std::string_view name(int i) const;
  uint32_t size(int i) const;  // uncompressed bytes
  // Index of the entry with this exact name, or -1.
  int find(std::string_view name) const;

  // Extracts entry i into out. Returns the bytes written, or -1 on a damaged
  // entry, an unsupported method, or when it does not fit in cap.
  int32_t extract(int i, uint8_t* out, uint32_t cap);

  // Extracts entry i piece by piece into `out`, for entries too large to
  // hold in memory. `window` holds kWindowBytes: the inflater's history.
  // Returns false on a damaged entry, an unsupported method or a failed write.
  static constexpr uint32_t kWindowBytes = 32768;
  bool extract_to(int i, Writer& out, uint8_t* window);

 private:
  // Where entry i's data starts in the file, after its local header; 0 on error.
  uint32_t data_offset(int i);

  struct Entry {
    uint32_t name_offset;
    uint32_t local_offset;
    uint32_t compressed;
    uint32_t size;
    uint16_t name_length;
    uint16_t method;
  };
  static constexpr uint32_t kChunk = 4096;

  File* file_ = nullptr;
  Entry* entries_;
  char* names_;
  uint8_t* chunk_;
  void* inflater_;  // tinfl_decompressor
  int count_ = 0;
};

}  // namespace vellum

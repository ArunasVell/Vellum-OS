#include "vellum/zip.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "miniz.h"

namespace vellum {
namespace {

constexpr uint32_t kEndSignature = 0x06054b50;
constexpr uint32_t kCentralSignature = 0x02014b50;
constexpr uint32_t kLocalSignature = 0x04034b50;
constexpr uint32_t kEndSize = 22;
constexpr uint32_t kCentralSize = 46;
constexpr uint32_t kLocalSize = 30;
constexpr uint16_t kStored = 0;
constexpr uint16_t kDeflated = 8;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

Zip::Zip()
    : entries_(new(std::nothrow) Entry[kMaxEntries]),
      names_(new(std::nothrow) char[kNameBytes]),
      chunk_(new(std::nothrow) uint8_t[kChunk]),
      inflater_(new(std::nothrow) tinfl_decompressor) {}

Zip::~Zip() {
  delete[] entries_;
  delete[] names_;
  delete[] chunk_;
  delete static_cast<tinfl_decompressor*>(inflater_);
}

std::string_view Zip::name(int i) const { return {names_ + entries_[i].name_offset, entries_[i].name_length}; }
uint32_t Zip::size(int i) const { return entries_[i].size; }

int Zip::find(std::string_view name) const {
  for (int i = 0; i < count_; ++i) {
    if (this->name(i) == name) return i;
  }
  return -1;
}

bool Zip::open(File* file) {
  close();
  if (file == nullptr || entries_ == nullptr || names_ == nullptr || chunk_ == nullptr || inflater_ == nullptr) {
    return false;
  }
  // The end record sits in the last 22 bytes, plus a comment of up to 64 KiB;
  // archives with long comments are rare, so only the last chunk is searched.
  const uint32_t file_size = file->size();
  if (file_size < kEndSize) return false;
  const uint32_t tail = std::min(file_size, kChunk);
  if (file->read_at(file_size - tail, chunk_, tail) != static_cast<int32_t>(tail)) return false;
  int end = -1;
  for (int i = static_cast<int>(tail - kEndSize); i >= 0; --i) {
    if (le32(chunk_ + i) == kEndSignature) {
      end = i;
      break;
    }
  }
  if (end < 0) return false;
  const uint32_t total = le16(chunk_ + end + 10);
  uint32_t offset = le32(chunk_ + end + 16);

  // Walk the central directory, one record at a time.
  uint32_t name_used = 0;
  for (uint32_t k = 0; k < total && count_ < kMaxEntries; ++k) {
    uint8_t h[kCentralSize];
    if (file->read_at(offset, h, kCentralSize) != static_cast<int32_t>(kCentralSize)) return false;
    if (le32(h) != kCentralSignature) return false;
    const uint16_t name_len = le16(h + 28);
    const uint32_t record = kCentralSize + name_len + le16(h + 30) + le16(h + 32);
    if (name_used + name_len > kNameBytes) break;
    if (file->read_at(offset + kCentralSize, names_ + name_used, name_len) != name_len) return false;
    entries_[count_++] = {name_used, le32(h + 42), le32(h + 20), le32(h + 24), name_len, le16(h + 10)};
    name_used += name_len;
    offset += record;
  }
  file_ = file;
  return true;
}

uint32_t Zip::data_offset(int i) {
  const Entry& e = entries_[i];
  uint8_t h[kLocalSize];
  if (file_->read_at(e.local_offset, h, kLocalSize) != static_cast<int32_t>(kLocalSize)) return 0;
  if (le32(h) != kLocalSignature) return 0;
  return e.local_offset + kLocalSize + le16(h + 26) + le16(h + 28);
}

int32_t Zip::extract(int i, uint8_t* out, uint32_t cap) { return unpack(i, out, cap, false); }

int32_t Zip::extract_head(int i, uint8_t* out, uint32_t cap) { return unpack(i, out, cap, true); }

int32_t Zip::unpack(int i, uint8_t* out, uint32_t cap, bool head) {
  if (file_ == nullptr || i < 0 || i >= count_) return -1;
  const Entry& e = entries_[i];
  if (e.size > cap && !head) return -1;
  uint32_t pos = data_offset(i);
  if (pos == 0) return -1;

  if (e.method == kStored) {
    const uint32_t n = std::min(e.size, cap);
    return file_->read_at(pos, out, n) == static_cast<int32_t>(n) ? static_cast<int32_t>(n) : -1;
  }
  if (e.method != kDeflated) return -1;

  auto* inflater = static_cast<tinfl_decompressor*>(inflater_);
  tinfl_init(inflater);
  uint32_t remaining = e.compressed;
  size_t written = 0;
  for (;;) {
    const uint32_t want = std::min(remaining, kChunk);
    if (want > 0 && file_->read_at(pos, chunk_, want) != static_cast<int32_t>(want)) return -1;
    pos += want;
    remaining -= want;
    size_t in_used = 0;
    // Feed this chunk until the inflater has consumed all of it.
    while (in_used < want || want == 0) {
      size_t in_size = want - in_used;
      size_t out_size = cap - written;
      const mz_uint32 flags =
          TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (remaining > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
      const tinfl_status status =
          tinfl_decompress(inflater, chunk_ + in_used, &in_size, out, out + written, &out_size, flags);
      in_used += in_size;
      written += out_size;
      if (status == TINFL_STATUS_DONE || (head && written == cap)) return static_cast<int32_t>(written);
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
        if (remaining == 0) return -1;  // truncated
        break;
      }
      if (status < TINFL_STATUS_DONE || status == TINFL_STATUS_HAS_MORE_OUTPUT) return -1;
      if (want == 0) return -1;
    }
  }
}

uint32_t Zip::stored_at(int i) {
  if (file_ == nullptr || i < 0 || i >= count_ || entries_[i].method != kStored) return 0;
  return data_offset(i);
}

bool Zip::extract_to(int i, Writer& out, uint8_t* window) {
  if (file_ == nullptr || i < 0 || i >= count_) return false;
  const Entry& e = entries_[i];
  uint32_t pos = data_offset(i);
  if (pos == 0) return false;
  if (e.method == kStored) {
    for (uint32_t done = 0; done < e.size;) {
      const uint32_t want = std::min(e.size - done, kChunk);
      if (file_->read_at(pos + done, chunk_, want) != static_cast<int32_t>(want) || !out.write(chunk_, want)) {
        return false;
      }
      done += want;
    }
    return true;
  }
  if (e.method != kDeflated) return false;

  // The window wraps: each time the inflater fills part of it, that part is
  // written out and later overwritten.
  auto* inflater = static_cast<tinfl_decompressor*>(inflater_);
  tinfl_init(inflater);
  uint32_t remaining = e.compressed;
  size_t window_pos = 0;
  uint32_t written = 0;
  for (;;) {
    const uint32_t want = std::min(remaining, kChunk);
    if (want > 0 && file_->read_at(pos, chunk_, want) != static_cast<int32_t>(want)) return false;
    pos += want;
    remaining -= want;
    size_t in_used = 0;
    for (;;) {
      size_t in_size = want - in_used;
      size_t out_size = kWindowBytes - window_pos;
      const mz_uint32 flags = remaining > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0;
      const tinfl_status status =
          tinfl_decompress(inflater, chunk_ + in_used, &in_size, window, window + window_pos, &out_size, flags);
      in_used += in_size;
      if (out_size > 0 && !out.write(window + window_pos, static_cast<uint32_t>(out_size))) return false;
      written += static_cast<uint32_t>(out_size);
      window_pos = (window_pos + out_size) & (kWindowBytes - 1);
      if (status == TINFL_STATUS_DONE) return written == e.size;
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
        if (remaining == 0) return false;  // truncated
        break;
      }
      if (status != TINFL_STATUS_HAS_MORE_OUTPUT) return false;
    }
  }
}

}  // namespace vellum

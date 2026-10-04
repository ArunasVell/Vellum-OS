// EPUB books: metadata and reading order from the package file, and each
// chapter's XHTML turned into plain paragraphs, one chapter at a time.
//
// Memory is allocated once, when the Epub is constructed: a buffer for the
// largest chapter and the paragraph list. Chapter text is converted in place.
#pragma once

#include <cstdint>

#include "vellum/document.h"
#include "vellum/hal.h"
#include "vellum/zip.h"

namespace vellum {

struct EpubInfo {
  char title[128];
  char author[96];
  char language[16];
};

class Epub : public Document {
 public:
  static constexpr int kMaxChapters = 1024;
  static constexpr uint32_t kBufferBytes = 2 * 1024 * 1024;  // largest chapter or package file
  static constexpr uint32_t kMaxParagraphs = 32768;

  Epub();
  ~Epub() override;
  Epub(const Epub&) = delete;
  Epub& operator=(const Epub&) = delete;

  // Opens the book and reads its metadata and reading order.
  bool open(Storage& storage, const char* path);
  void close();
  bool is_open() const { return file_ != nullptr; }
  const EpubInfo& info() const { return info_; }

  const char* language() const override { return info_.language; }
  int chapter_count() const override { return spine_count_; }
  uint32_t chapter_weight(int i) const override;
  bool load_chapter(int i, Chapter& out) override;

  // Converts XHTML in buf[0, len) to paragraphs in place; buf[len] must be
  // writable. Fills paragraphs (up to max) and returns how many. Exposed for tests.
  static uint32_t to_paragraphs(char* buf, uint32_t len, const char** paragraphs, uint32_t max);

 private:
  struct ManifestItem;
  // Extracts entry i into buf_ and NUL-terminates it. Returns its length or -1.
  int32_t read_entry(int i);
  bool read_package(const char* opf_path);

  Storage* storage_ = nullptr;
  File* file_ = nullptr;
  Zip zip_;
  char* buf_;
  const char** paragraphs_;
  ManifestItem* manifest_;
  uint16_t spine_[kMaxChapters];
  int spine_count_ = 0;
  int loaded_ = -1;
  uint32_t paragraph_count_ = 0;
  EpubInfo info_{};
};

}  // namespace vellum

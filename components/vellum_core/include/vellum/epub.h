// EPUB books: metadata and reading order from the package file, and each
// chapter's XHTML turned into plain paragraphs, one chapter at a time.
//
// Memory is allocated once, when the Epub is constructed: a buffer for the
// largest chapter and the paragraph list. Chapter text is converted in place.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/document.h"
#include "vellum/hal.h"
#include "vellum/zip.h"

namespace vellum {

struct EpubInfo {
  char title[128];
  char author[96];
  char language[16];
};

// Called for each id in a chapter with the paragraph it starts in.
struct AnchorHook {
  void (*found)(void* ctx, std::string_view id, uint32_t paragraph) = nullptr;
  void* ctx = nullptr;
};

class Epub : public Document {
 public:
  static constexpr int kMaxChapters = 1024;
  static constexpr uint32_t kBufferBytes = 2 * 1024 * 1024;  // largest chapter or package file
  static constexpr uint32_t kMaxParagraphs = 32768;
  static constexpr int kMaxToc = 1024;

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
  // Also places the contents entries that point into the chapter.
  bool load_chapter(int i, Chapter& out) override;
  int toc_count() const override { return toc_count_; }
  const TocEntry& toc(int i) const override { return toc_[i]; }

  // Converts XHTML in buf[0, len) to paragraphs in place; buf[len] must be
  // writable. Fills paragraphs (up to max) and returns how many. Exposed for tests.
  static uint32_t to_paragraphs(char* buf, uint32_t len, const char** paragraphs, uint32_t max,
                                AnchorHook anchors = {});

 private:
  struct ManifestItem;
  // Extracts entry i into buf_ and NUL-terminates it. Returns its length or -1.
  int32_t read_entry(int i);
  bool read_package(const char* opf_path);
  // Table of contents from an EPUB 3 navigation document or an EPUB 2 NCX file.
  void read_nav(const char* path);
  void read_ncx(const char* path);
  // Adds an entry for `href` (relative to `base`) when it points into the reading order.
  void add_toc(std::string_view base, std::string_view href, const char* title, int depth);

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
  TocEntry* toc_;
  char (*toc_anchor_)[48];  // fragment each entry points to; empty for a chapter's start
  int toc_count_ = 0;
  EpubInfo info_{};
};

}  // namespace vellum

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

// A place in a chapter that links can lead to: an element with an id.
struct Anchor {
  uint32_t hash;       // anchor_hash(id)
  uint32_t paragraph;  // where the element starts
  uint32_t end;        // one past the paragraph where it ends
  bool note;           // a footnote or endnote: an <aside>, or marked so (epub:type, role, class)
};

// A rule of the book's stylesheet. Only simple selectors are read: tag,
// .class and tag.class (each of a list); the rest of the sheet is ignored.
struct CssRule {
  uint32_t tag = 0;    // anchor_hash of the tag name in lowercase; 0 for any
  uint32_t cls = 0;    // of the class name; 0 for any
  int8_t align = -1;   // -1 not set, 0 left or justified, 1 centred, 2 flush right
  int8_t indent = -1;  // -1 not set, 0 flush, 1 indented
  int8_t bold = -1;    // -1 not set, 0 normal, 1 bold
  int8_t italic = -1;  // -1 not set, 0 upright, 1 italic
  int8_t marker = -1;  // list bullets: -1 not set, 0 none, 1 shown
};

// What to_paragraphs collects besides the paragraphs, into the caller's
// arrays (any may be missing: max 0). Counts are set on return.
struct Markup {
  StyleRun* runs = nullptr;
  uint32_t max_runs = 0;
  uint32_t run_count = 0;
  Link* links = nullptr;
  uint32_t max_links = 0;
  uint32_t link_count = 0;
  Picture* pictures = nullptr;  // without them, <img> is left out
  uint32_t max_pictures = 0;
  uint32_t picture_count = 0;
  Anchor* anchors = nullptr;
  uint32_t max_anchors = 0;
  uint32_t anchor_count = 0;
  const CssRule* css = nullptr;  // the stylesheets, in order
  uint32_t css_count = 0;
  // The chapter a link's path (without its #fragment) names, and the archive
  // entry of a picture's src; -1 for none. Without them, links lead only
  // within the chapter.
  int (*chapter_of)(void* ctx, std::string_view href) = nullptr;
  int (*entry_of)(void* ctx, std::string_view src) = nullptr;
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
  // The cover picture (JPEG or PNG) as stored in the book, or -1 when the
  // book names none. The data stays valid until the next chapter or cover
  // is read.
  int32_t read_cover(const uint8_t** data);

  const char* language() const override { return info_.language; }
  int chapter_count() const override { return spine_count_; }
  uint32_t chapter_weight(int i) const override;
  // Also places the contents entries that point into the chapter.
  bool load_chapter(int i, Chapter& out) override;
  int toc_count() const override { return toc_count_; }
  const TocEntry& toc(int i) const override { return toc_[i]; }

  ChapterMarkup markup() const override;
  bool follow(const Link& link, LinkTarget& out, char* note, size_t size) override;
  bool draw_picture(Framebuffer& fb, const Picture& p, Rect r) override;

  // Converts XHTML in buf[0, len) to paragraphs in place; buf[len] must be
  // writable. Fills paragraphs (up to max) and returns how many. Headings,
  // pictures, rules, list items, quotations, table rows and the book's
  // alignment and indents become marks (kHeadingMark...). With `markup`,
  // also collects emphasis, links, pictures and ids. Exposed for tests.
  static uint32_t to_paragraphs(char* buf, uint32_t len, const char** paragraphs, uint32_t max, AnchorHook anchors = {},
                                Markup* markup = nullptr);
  // Adds the rules of a stylesheet to out[count, max); returns the new count.
  static uint32_t parse_css(std::string_view css, CssRule* out, uint32_t count, uint32_t max);

  static constexpr uint32_t kMaxRuns = 4096;
  static constexpr uint32_t kMaxLinks = 1024;
  static constexpr uint32_t kMaxPictures = 256;
  static constexpr uint32_t kMaxAnchors = 2048;
  static constexpr uint32_t kMaxCss = 256;

 private:
  struct ManifestItem;
  // Extracts entry i into buf_ and NUL-terminates it. Returns its length or -1.
  int32_t read_entry(int i);
  // The stylesheets the chapter in buf_[0, len) links to or holds, into css_.
  void read_css(int entry, uint32_t len);
  // Sizes of the loaded chapter's pictures, from their first bytes.
  void measure_pictures();
  // The anchor with this hash in the loaded chapter, or nullptr.
  const Anchor* find_anchor(uint32_t hash) const;
  // buf_ past the loaded chapter's text, free for pictures and stylesheets.
  uint8_t* spare(uint32_t& size) const;
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
  int cover_ = -1;  // archive entry of the cover picture
  EpubInfo info_{};
  uint32_t key_ = 0;            // tells books apart for their kept pictures
  uint32_t chapter_bytes_ = 0;  // of buf_, taken by the loaded chapter
  // The loaded chapter's markup, allocated once.
  StyleRun* runs_;
  Link* links_;
  Picture* pictures_;
  Anchor* anchors_;
  CssRule* css_;
  uint32_t run_count_ = 0;
  uint32_t link_count_ = 0;
  uint32_t picture_count_ = 0;
  uint32_t anchor_count_ = 0;
  uint32_t css_count_ = 0;
};

}  // namespace vellum

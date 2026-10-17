// Books that are one long file rather than an EPUB archive: plain text
// (.txt), FictionBook (.fb2, or zipped as .fb2.zip) and single web pages
// (.html, .htm). Kindle books, PDFs, Word, Markdown and RTF files are first
// turned into a web page (see convert.h), once, kept in kConvertFolder.
// Comics (.cbz, a ZIP of pictures) are read a page per picture.
//
// Opening a book reads through it once, a piece at a time, to find its
// chapters; a chapter is read and converted only when it is shown, so files
// of many megabytes need one chapter in memory. Chapters follow the book's
// own headings (FB2 sections, HTML h1/h2, "Chapter 1" lines in text) and
// long stretches without one are cut into parts at paragraph ends.
//
// Memory is allocated once, when the TextBook is constructed.
#pragma once

#include <cstdint>
#include <string_view>

#include "vellum/charset.h"
#include "vellum/convert.h"
#include "vellum/document.h"
#include "vellum/epub.h"
#include "vellum/hal.h"
#include "vellum/zip.h"

namespace vellum {

class TextBook : public Document {
 public:
  enum class Format : uint8_t { None, Text, Fb2, Fb2Zip, Html, Mobi, Pdf, Docx, Markdown, Rtf, Comic };

  static constexpr int kMaxChapters = 1024;
  static constexpr int kMaxToc = 1024;
  static constexpr uint32_t kChapterBytes = 128 * 1024;  // most bytes of the file in one chapter
  static constexpr uint32_t kSplitBytes = 96 * 1024;     // a part this long ends at the next paragraph
  static constexpr uint32_t kMaxParagraphs = 16384;
  static constexpr uint32_t kSpare = 8192;  // room for heading marks: text grows by a byte per heading
  // A zipped FB2 is unpacked here once, so chapters can be read from it.
  static constexpr const char* kUnzipFolder = "vellum/unzipped";
  // Converted books, their pictures, and a note for each one that cannot be.
  static constexpr const char* kConvertFolder = "vellum/converted";
  static constexpr int kComicPages = 20;  // pictures in each chapter of a comic
  static constexpr uint32_t kMaxRuns = 4096;
  static constexpr uint32_t kMaxPictures = 256;

  // The format a file name says it is in (by its extension, any case).
  static Format format_of(const char* path);

  TextBook();
  ~TextBook() override;
  TextBook(const TextBook&) = delete;
  TextBook& operator=(const TextBook&) = delete;

  // Opens the book and finds its details and chapters. A book that needs
  // converting is converted first (on_convert is told), unless `convert`
  // is false: then it is not opened, and waiting() says so.
  bool open(Storage& storage, const char* path, bool convert = true);
  // Told when a book starts being converted, which can take a while, and
  // then how far it is (`done` of `total`); `progress` returns false to
  // stop it, and the book is not opened (last_convert() is Cancelled).
  using ConvertFn = void (*)(void* ctx, const char* path);
  using ProgressFn = bool (*)(void* ctx, uint32_t done, uint32_t total);
  void on_convert(ConvertFn fn, ProgressFn progress, void* ctx) {
    convert_fn_ = fn;
    progress_fn_ = progress;
    convert_ctx_ = ctx;
  }
  // Why the last book that needed converting could not be opened.
  convert::Result last_convert() const { return last_convert_; }
  // The last book was not opened only because it is not converted yet.
  bool waiting() const { return waiting_; }
  // The title and author a file name gives ("Author - Title.pdf"), or with
  // `split` false the whole name as the title.
  static void title_from_name(const char* path, EpubInfo& info, bool split = true);
  void close();
  bool is_open() const { return file_ != nullptr; }
  Format format() const { return format_; }
  const EpubInfo& info() const { return info_; }
  charset::Encoding encoding() const { return encoding_; }
  // The cover picture of an FB2 book (JPEG or PNG), or -1 when it has none.
  // The data stays valid until the next chapter or cover is read.
  int32_t read_cover(const uint8_t** data);
  // Makes the cover thumbnails (covers::make) from whatever the book has:
  // an FB2 cover, a converted book's cover picture, a comic's first page.
  bool make_cover(const char* cover_path);

  const char* language() const override { return info_.language; }
  int chapter_count() const override { return part_count_; }
  uint32_t chapter_weight(int i) const override;
  bool load_chapter(int i, Chapter& out) override;
  int toc_count() const override { return toc_count_; }
  const TocEntry& toc(int i) const override { return toc_[i]; }
  ChapterMarkup markup() const override;
  bool draw_picture(Framebuffer& fb, const Picture& p, Rect r) override;

  // The book at `path` (`size` bytes) is being deleted: removes what was
  // made from it for reading (an unpacked FB2, a converted page with its
  // pictures, or the note that it could not be converted).
  static void remove_copies(Storage& storage, const char* path, uint32_t size);

  // Plain text rules, exposed for tests.
  //
  // Whether a line of text (UTF-8, without its line break) is a chapter
  // heading: "Chapter 1", "CHAPTER IV", "Part Two", "Skyrius 3",
  // "1 skyrius", "Pirmas skyrius", "XII", "IV.", "Prologue"...
  static bool is_heading(std::string_view line);
  // Splits UTF-8 text into paragraphs, written to `out` with `in`'s line
  // breaks turned into NULs (out may be `in`, or before it in the same
  // buffer). Wrapped text (lines broken at a fixed width) joins lines up to
  // a blank or indented line; otherwise every line is a paragraph. Heading
  // lines get kHeadingMark, which needs one byte more than the line: at most
  // `spare` of them get it. Returns the paragraph count.
  static uint32_t split_text(const char* in, uint32_t len, char* out, const char** paragraphs, uint32_t max,
                             bool wrapped, uint32_t spare = 0xFFFFFFFF);
  // Converts the XML of an FB2 section (UTF-8) to paragraphs in place, as
  // Epub::to_paragraphs does for XHTML. on_section(ctx, n, paragraph) is
  // called for the n-th <section> (from 1) with the paragraph it starts.
  using SectionFn = void (*)(void* ctx, uint32_t n, uint32_t paragraph);
  static uint32_t fb2_paragraphs(char* buf, uint32_t len, const char** paragraphs, uint32_t max,
                                 SectionFn on_section = nullptr, void* ctx = nullptr);

 private:
  struct Part {
    uint32_t offset;
    uint32_t length;
  };

  // Unpacks the .fb2 inside a .fb2.zip into kUnzipFolder (once) and opens it.
  bool open_zipped(const char* path);
  // Converts the book into kConvertFolder (once) and opens the web page.
  bool open_converted(const char* path, convert::Kind kind, bool convert);
  // A comic: its pictures in order, chapters of kComicPages.
  bool open_comic(const char* path);
  void load_comic(int i);
  // Whether the book is read as a web page (converted ones are).
  bool html() const {
    return format_ == Format::Html || format_ == Format::Mobi || format_ == Format::Pdf || format_ == Format::Docx ||
           format_ == Format::Markdown || format_ == Format::Rtf;
  }
  // The file of picture `entry` (a web page's), or nullptr.
  const char* picture_file(int32_t entry) const;
  // Markup::entry_of for web pages: the picture file `src` names, kept.
  static int picture_entry(void* ctx, std::string_view src);
  void measure_pictures();
  bool scan_text();
  bool scan_fb2();
  bool scan_html();
  // Starts a chapter at `offset`; ends the one before it there.
  bool begin_part(uint32_t offset);
  void end_part(uint32_t offset);
  // Adds a contents entry for the current chapter.
  void add_toc(std::string_view title, int depth, uint32_t paragraph, uint16_t section = 0);
  // Reads bytes [offset, offset + len) and converts them to UTF-8 at
  // text_ + at. Returns the UTF-8 length.
  void title_from_name(const char* path);
  void guess_language(const char* text, uint32_t len);

  Storage* storage_ = nullptr;
  File* file_ = nullptr;
  Zip zip_;
  Format format_ = Format::None;
  charset::Encoding encoding_ = charset::Encoding::Utf8;
  bool wrapped_ = false;   // plain text broken into lines at a fixed width
  int annotation_ = -1;    // FB2: the chapter that is the book's annotation
  uint32_t cover_at_ = 0;  // FB2: the cover's base64 text in the file
  uint32_t cover_len_ = 0;
  uint8_t* raw_;  // the file's bytes, a chapter or a scanning window
  char* text_;    // the current chapter as UTF-8, split into paragraphs
  const char** paragraphs_;
  uint32_t paragraph_count_ = 0;
  Part* parts_;
  int part_count_ = 0;
  bool part_open_ = false;  // the last part's end is not known yet
  int loaded_ = -1;
  TocEntry* toc_;
  uint16_t* toc_section_;  // FB2: the entry's <section> within its chapter (from 1); 0 = none
  int toc_count_ = 0;
  char heading_[2][130] = {};  // the annotation page's title and author, as headings
  EpubInfo info_{};
  // Emphasis and pictures of the loaded chapter (web pages and comics).
  StyleRun* runs_;
  uint32_t run_count_ = 0;
  Picture* pictures_;
  uint32_t picture_count_ = 0;
  char* picture_paths_;  // web pages: the files pictures name, NUL after each
  uint32_t picture_paths_used_ = 0;
  uint32_t picture_at_[kMaxPictures] = {};
  uint32_t picture_count_now_ = 0;  // pictures named so far while a chapter is read
  char base_[160] = {};             // the web page's folder, for pictures it names
  char cover_[192] = {};            // a converted book's cover picture file
  uint32_t key_ = 0;                // the book's pictures are kept under this
  uint16_t* comic_;                 // a comic's pictures, as archive entries in reading order
  int comic_count_ = 0;
  bool contents_from_marks_ = false;
  ConvertFn convert_fn_ = nullptr;
  ProgressFn progress_fn_ = nullptr;
  void* convert_ctx_ = nullptr;
  bool waiting_ = false;
  convert::Result last_convert_ = convert::Result::Ok;
};

}  // namespace vellum

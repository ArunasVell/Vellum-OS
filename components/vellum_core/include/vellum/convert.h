// Books in formats Vellum does not lay out itself, turned once into a web
// page (UTF-8 HTML) that TextBook then reads like any other: Kindle books
// without DRM (.mobi, .azw3, .azw, .prc, .pdb), PDFs that hold text, Word
// (.docx), Markdown (.md) and RTF.
//
// The page keeps what the reader can show: paragraphs, headings, bold and
// italics, lists, pictures (written next to the page as files) and the
// book's own contents, as <vellum-chapter> marks where chapters begin.
//
// Converting reads the book once from start to end; it needs kWorkBytes of
// memory, which the caller lends for that time only. A PDF also borrows a
// little more for itself, to go faster, when there is some to spare.
#pragma once

#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

class Zip;

namespace convert {

enum class Kind : uint8_t { None, Mobi, Pdf, Docx, Markdown, Rtf };

// The kind a file name says it is (by its extension, any case).
Kind kind_of(const char* path);

enum class Result : uint8_t {
  Ok,
  Unreadable,  // damaged, or not what its name says
  Protected,   // DRM or a password: it cannot be read
  NoText,      // a PDF of scanned pages, pictures only
  NoMemory,
  Cancelled,  // stopped by Job::progress: it can be tried again
};

constexpr uint32_t kWorkBytes = 1536 * 1024;

// The marks a converted page carries for TextBook (and any web page may):
// <vellum-chapter depth="0" title="..."/> starts a chapter with that
// contents entry, and <meta name="vellum-contents" content="marks"> in the
// head says the book's contents come from those marks alone, not from its
// headings. <meta name="cover" content="file"> names the cover picture.
constexpr const char* kChapterTag = "vellum-chapter";

struct Job {
  Storage* storage = nullptr;
  File* in = nullptr;         // the book
  const char* in_path = "";   // its path, for pictures it names (Markdown)
  const char* out_path = "";  // the web page, written only when all went well
  // Pictures are written to `pictures` + a name, e.g. "vellum/converted/1a2b3c4d-12.jpg";
  // the page names them relative to itself (the part after the last '/').
  const char* pictures = "";
  uint8_t* work = nullptr;  // kWorkBytes
  uint32_t work_size = 0;
  Zip* zip = nullptr;  // for .docx
  // A temporary file a converter may write before the page and read back
  // while writing it (the storage has one writer at a time).
  const char* scratch_path = "";
  // Told how far it is, now and then (a PDF after each page: `done` of
  // `total` pages); returning false stops it with Result::Cancelled.
  bool (*progress)(void* ctx, uint32_t done, uint32_t total) = nullptr;
  void* progress_ctx = nullptr;
};

Result run(Kind kind, Job& job);

Result mobi(Job& job);
Result pdf(Job& job);
Result docx(Job& job);
Result markdown(Job& job);
Result rtf(Job& job);

}  // namespace convert
}  // namespace vellum

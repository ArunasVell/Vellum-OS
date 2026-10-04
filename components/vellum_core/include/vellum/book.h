// A book as the reader sees it: metadata and paragraphs of UTF-8 text.
#pragma once

#include <cstdint>
#include <span>

namespace vellum {

struct Book {
  const char* title;
  const char* author;
  const char* chapter_label;  // e.g. "Chapter I"
  const char* chapter_title;  // may be empty
  const char* language;       // BCP 47 tag, e.g. "en" or "lt"
  const char* const* paragraphs;
  uint32_t paragraph_count;
};

// Public-domain samples built into the firmware until books can be opened
// from the SD card.
namespace books {
extern const Book alice;
extern const Book moby_dick;
extern const Book persuasion;
extern const Book father_brown;
}  // namespace books

// The built-in books in library order (by title).
std::span<const Book* const> builtin_books();

}  // namespace vellum

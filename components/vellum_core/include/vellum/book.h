// A book as the reader sees it: metadata and paragraphs of UTF-8 text.
#pragma once

#include <cstdint>

namespace vellum {

struct Book {
  const char* title;
  const char* author;
  const char* chapter_label;  // e.g. "Chapter I"
  const char* chapter_title;
  const char* language;  // BCP 47 tag, e.g. "en" or "lt"
  const char* const* paragraphs;
  uint32_t paragraph_count;
};

// Built-in sample until books can be opened from the SD card.
const Book& sample_book();

}  // namespace vellum

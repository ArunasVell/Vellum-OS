// Interface text in each supported language.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vellum {

enum class Language : uint8_t { English, Lithuanian };

struct Strings {
  const char* language_name;  // in the language itself
  // Home and library
  const char* continue_reading;  // uppercase label
  const char* start_reading;     // uppercase label
  const char* library_label;     // uppercase label
  const char* library;
  const char* all_books;
  const char* settings;
  const char* not_started;
  const char* page_of;   // printf: page, count ("Page 3 of 20")
  const char* short_of;  // printf: page, count ("3 of 20"), reader footer
  const char* status_new;
  const char* status_finished;
  const char* hint_home;
  const char* hint_list;
  const char* hint_settings;
  // Settings
  const char* general_label;  // uppercase label
  const char* reading_label;  // uppercase label
  const char* preview_label;  // uppercase label
  const char* language;
  const char* clock;
  const char* clock_24;
  const char* clock_12;
  const char* text_size;
  const char* size_names[4];  // small .. extra large
  const char* line_spacing;
  const char* word_spacing;
  const char* spacing_names[3];  // narrow, normal, wide (spaces between lines or words)
  const char* margins;
  const char* margin_names[3];  // narrow, normal, wide
  const char* alignment;
  const char* justified;
  const char* left;
  const char* hyphenation;
  const char* on;
  const char* off;
  const char* preview_text;      // sample paragraph for the settings preview
  const char* preview_language;  // its BCP 47 tag, for hyphenation
};

const Strings& strings(Language lang);

// "4 books", with the plural form the language needs.
void format_book_count(Language lang, int n, char* out, size_t size);

}  // namespace vellum

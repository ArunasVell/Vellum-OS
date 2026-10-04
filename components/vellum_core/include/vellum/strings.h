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
  const char* sleep_after;
  const char* sleep_names[4];  // 5, 10, 30 minutes, never
  const char* sleeping;        // uppercase label on the sleep screen
  const char* wake_hint;
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
  // Adding books over Wi-Fi
  const char* add_books;      // home link and screen title
  const char* transfer_lead;  // one line under the title
  const char* step_join;      // uppercase label
  const char* network;
  const char* password;
  const char* step_open;       // uppercase label
  const char* scan_hint;       // the QR codes work with a phone camera
  const char* received_label;  // uppercase label
  const char* waiting;         // nothing received yet
  const char* receiving;       // printf: file name
  const char* card_full;
  const char* starting;
  const char* no_wifi;
  const char* no_card;
  const char* hint_transfer;
  // Contents and bookmarks, opened from the reader
  const char* contents;  // screen title
  const char* back_to_book;
  const char* add_bookmark;
  const char* remove_bookmark;
  const char* bookmarks_label;  // uppercase label
  const char* chapters_label;   // uppercase label
  const char* chapter_n;        // printf: number, for books without a table of contents
  // Battery history
  const char* battery;  // settings row and screen title
  const char* on_battery;
  const char* charging;
  const char* history_label;    // uppercase label over the chart
  const char* estimates_label;  // uppercase label
  const char* time_left;
  const char* while_reading;
  const char* while_asleep;
  const char* measuring;    // not enough data yet
  const char* per_hour;     // printf: a number such as "4.2"
  const char* per_day;      // printf: a number such as "0.8"
  const char* clock_unset;  // why there is no history yet
  const char* hint_battery;
  const char* preview_text;      // sample paragraph for the settings preview
  const char* preview_language;  // its BCP 47 tag, for hyphenation
};

const Strings& strings(Language lang);

// "4 books", with the plural form the language needs.
void format_book_count(Language lang, int n, char* out, size_t size);
// "About 3 days" or "About 5 hours" for a duration in hours.
void format_time_left(Language lang, int hours, char* out, size_t size);
// A number given in tenths, with the language's decimal mark: "4.2", "4,2".
void format_tenths(Language lang, int tenths, char* out, size_t size);

}  // namespace vellum

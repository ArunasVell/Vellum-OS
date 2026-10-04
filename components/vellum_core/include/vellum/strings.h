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
  const char* updating;           // printf: percent
  const char* update_done;        // full screen, before the restart
  const char* update_not_vellum;  // the file sent was something else
  const char* update_broken;      // it stopped part way
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
  // Front light
  const char* front_light;  // settings row, contents row and screen title
  const char* brightness;
  const char* warmth;
  const char* cool;  // the warmth slider's two ends
  const char* warm;
  const char* done;
  const char* hint_light;
  // Device check
  const char* device_check;   // settings row and screen title
  const char* buttons_label;  // uppercase label
  const char* key_names[4];   // Prev, Next, Home, Power
  const char* touch_label;    // uppercase label
  const char* touch_here;
  const char* about_label;  // uppercase label
  const char* device;
  const char* screen;
  const char* memory_card;
  const char* wifi;
  const char* version;
  const char* present;
  const char* missing;
  const char* not_set;
  const char* hint_check;
  // Updates
  const char* updates;       // settings row and screen title
  const char* this_version;  // printf: version
  const char* status_label;  // uppercase label
  const char* up_to_date;
  const char* update_ready;  // printf: version
  const char* not_checked;
  const char* checked_at;  // printf: date and time
  const char* checking;
  const char* installing;  // printf: version, percent
  const char* need_home_wifi;
  const char* wifi_failed;
  const char* server_failed;
  const char* install_failed;
  const char* check_now;
  const char* install_version;  // printf: version
  const char* automatic;
  const char* auto_names[3];  // at night, ask first, off
  const char* auto_help[3];   // what each does
  const char* night_needs;    // what the nightly check needs and lacks
  const char* home_wifi;
  const char* from_file;
  const char* hint_updates;
  const char* setup_lead;   // one line under the title of the phone screen
  const char* phone_label;  // uppercase label
  const char* waiting_phone;
  const char* wifi_saved;  // printf: network name
  const char* hint_setup;
  const char* update_notice;     // printf: version; small notice on the home screen
  const char* update_new;        // settings row value when one is ready
  // Sending books reader to reader
  const char* send_book;     // contents row
  const char* send_title;    // screen title
  const char* nearby_label;  // uppercase label
  const char* looking;
  const char* sending_to;  // printf: reader name
  const char* sent_to;     // printf: reader name
  const char* send_no_answer;
  const char* send_refused;
  const char* send_card_full;
  const char* send_corrupt;
  const char* send_unreadable;
  const char* hint_send;
  const char* hint_sending;
  const char* hint_sent;
  const char* hint_retry;
  const char* peer_hint;         // on "Add books"
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

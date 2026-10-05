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
  const char* reading;  // reading settings: row and screen title
  const char* menu_font;
  const char* book_font;
  const char* preview_label;  // uppercase label
  const char* language;
  const char* clock;
  const char* clock_24;
  const char* clock_12;
  const char* sleep_after;
  const char* sleep_names[4];  // 5, 10, 30 minutes, never
  const char* sleeping;        // uppercase label on the sleep screen
  const char* wake_hint;
  const char* battery_empty;  // the sleep screen when the battery runs out
  const char* charge_hint;
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
  const char* free_memory;  // device check row: free RAM, internal · external
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
  const char* update_notice;  // printf: version; small notice on the home screen
  const char* update_new;     // settings row value when one is ready
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
  const char* peer_hint;  // on "Add books"
  // The free book store
  const char* store_link;        // last row of the library
  const char* store_search_for;  // the same row while searching: %s is the words
  const char* store_title;       // screen title
  const char* store_lead;        // one line under the title
  const char* store_demo;        // the simulators' canned catalogue
  const char* store_abouts[2];   // a line under each catalogue's name
  const char* store_lists[9];    // Gutenberg: popular, new, Lithuanian, 5 subjects; Standard Ebooks: new releases
  const char* store_loading;
  const char* store_earlier;  // row before a list page
  const char* store_more;     // row after it
  const char* store_empty;
  const char* store_no_epub;
  const char* store_download;
  const char* store_downloading;  // printf: percent
  const char* store_saved;
  const char* store_read_now;
  const char* store_back;  // to the list
  const char* store_retry;
  const char* store_no_home_wifi;  // says where to set it
  const char* store_no_server;
  const char* store_broke_off;
  const char* store_not_book;
  const char* hint_store_busy;
  // The phone page on the home Wi-Fi, and KOReader sync
  const char* step_home;         // uppercase label on "Add books"
  const char* home_hint;         // under the network's name
  const char* use_own_network;   // row on "Add books"
  const char* use_home_wifi;     // the same the other way; printf: network name
  const char* home_wifi_failed;  // why the reader's own network opened instead
  const char* sync_progress;     // contents row
  const char* sync_busy;
  const char* sync_sent;   // printf: percent
  const char* sync_moved;  // printf: percent
  const char* sync_up_to_date;
  const char* sync_errors[5];  // no home Wi-Fi, could not join it, no server, wrong login, name taken
  // Words: dictionaries and highlights
  const char* look_up_word;      // contents row
  const char* highlights_label;  // uppercase label in the contents
  const char* highlight;         // button under a definition
  const char* remove_highlight;  // the same, on a highlighted word
  const char* close;
  const char* save;
  const char* cancel;
  const char* no_dictionary;  // no dictionary on the card
  const char* not_in_dictionary;
  const char* preparing_dictionary;
  const char* hint_pick_line;  // choosing a line with the side buttons
  const char* hint_pick_word;  // then a word
  const char* hint_extend;     // choosing where a highlight ends
  // Reading stats and time left
  const char* footer;           // reading settings row: the middle of the reader's footer
  const char* footer_names[3];  // page number, time left in chapter, percent read
  const char* footer_left;      // printf: a duration ("12 min left in chapter")
  const char* contents_left;    // printf: duration in chapter, in book; line under the contents title
  const char* reading_stats;    // settings row and screen title
  const char* today_label;      // uppercase label
  const char* week_label;       // uppercase label over the chart of minutes
  const char* weekdays[7];      // short names, Monday first
  const char* this_month;
  const char* reading_speed;
  const char* pages_an_hour;  // printf: number
  const char* all_time;
  const char* books_label;       // uppercase label
  const char* nothing_read;      // no reading recorded yet
  const char* stats_clock;       // without the clock there are no days
  const char* preview_text;      // sample paragraph for the settings preview
  const char* preview_language;  // its BCP 47 tag, for hyphenation
  // Library: order, search, shelves
  const char* sort_by;        // printf: an order name ("Sort: Title")
  const char* sort_names[3];  // recent, title, author
  const char* search;         // button on the library screen
  const char* search_prompt;  // in the keyboard's empty text line
  const char* ok;             // the keyboard's finishing key
  const char* tab_all;        // library tabs: every book,
  const char* tab_reading;    // started and not finished,
  const char* tab_finished;   // and finished
  const char* new_shelf;      // the last tab, and a row on the shelves screen
  const char* shelf_name;     // in the keyboard's empty text line
  const char* rename;         // a shelf
  const char* delete_shelf;
  const char* delete_again;      // the same button, asking to be pressed again
  const char* shelf_empty;       // a shelf with no books
  const char* nothing_found;     // printf: the search
  const char* no_books_here;     // Reading now or Finished, empty
  const char* add_to_shelf;      // contents row
  const char* shelves;           // screen title: a book's shelves
  const char* shelves_label;     // uppercase label
  const char* finished_reading;  // the toggle on that screen
};

const Strings& strings(Language lang);

// "4 books", with the plural form the language needs.
void format_book_count(Language lang, int n, char* out, size_t size);
// "About 3 days" or "About 5 hours" for a duration in hours.
void format_time_left(Language lang, int hours, char* out, size_t size);
// A number given in tenths, with the language's decimal mark: "4.2", "4,2".
void format_tenths(Language lang, int tenths, char* out, size_t size);
// A reading time: "12 min", "4 h 10 min".
void format_duration(Language lang, int minutes, char* out, size_t size);
// "1 page", "12 pages".
void format_page_count(Language lang, int n, char* out, size_t size);

}  // namespace vellum

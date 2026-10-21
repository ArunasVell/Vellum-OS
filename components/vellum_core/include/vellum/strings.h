// Interface text in each supported language.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vellum {

// Saved by number: new languages go at the end.
enum class Language : uint8_t {
  English,
  Lithuanian,
  German,
  French,
  Spanish,
  Italian,
  Polish,
  Latvian,
  Estonian,
  Ukrainian
};
constexpr int kLanguageCount = 10;

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
  const char* menu_text;
  const char* menu_text_names[2];  // normal, large
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
  const char* to_home_screen;  // a Contents row: closes the book
  const char* add_bookmark;
  const char* remove_bookmark;
  const char* bookmarks_label;  // uppercase label
  const char* chapters_label;   // uppercase label
  const char* chapter_n;        // printf: number, for books without a table of contents
  // Battery history
  const char* battery;  // settings row and screen title
  const char* on_battery;
  const char* page_turn_ms;  // printf: the last page turn's time, ms
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
  const char* scan_one_hint;     // scan_hint with one code
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
  const char* sort_names[4];  // recent, title, author, recently added
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
  // Buttons, screen orientation, automatic page turns, screenshots
  const char* buttons;            // settings row and screen title
  const char* button_pages[2];    // while reading, in menus
  const char* input_names[7];     // ButtonInput: Prev, Next, Power, Home, then Prev, Next, Home held
  const char* action_names[22];   // ButtonAction, in its order
  const char* reset_default;      // a row on the buttons screen
  const char* power_hold_note;    // holding Power always sleeps
  const char* way_back_note;      // why some choices are passed over
  const char* screen_names[4];    // Orientation (the row is `screen`)
  const char* screen_note;        // landscape turns books only, under the row
  const char* auto_turn;          // reading settings row
  const char* every_seconds;      // printf: seconds ("Every 30 s")
  const char* full_refresh;       // reading settings row
  const char* refresh_names[5];   // FullRefresh
  const char* start_auto_turn;    // contents row; printf: seconds
  const char* auto_turn_on;       // notice; printf: seconds
  const char* auto_turn_off;      // notice
  const char* screenshot_saved;   // notice; printf: file name
  const char* screenshot_failed;  // notice
  // Sleep screens
  const char* sleep_screen;           // settings row and screen title
  const char* sleep_kinds[5];         // book cover, my pictures, reading stats, minimal, page as left
  const char* sleep_help[5];          // a line about each
  const char* pictures_label;         // uppercase label
  const char* sleep_order;            // row: which of the pictures
  const char* sleep_order_names[2];   // shuffle, one picture
  const char* choose_pictures;        // row: opens the pictures
  const char* preview;                // row: shows the sleep screen
  const char* no_pictures;            // the sleep folder is empty
  const char* preparing_pictures;     // while converting them
  const char* preparing_book;         // a book in a new format, converted the first time
  const char* preparing_book_once;    // under it
  const char* preparing_book_stop;    // at the bottom: how to stop it
  const char* picture_broken;         // under a picture that cannot be shown
  const char* pictures_shuffle_lead;  // line under the pictures' title
  const char* pictures_one_lead;
  const char* hint_pictures_shuffle;
  const char* hint_pictures_one;
  const char* hint_preview;
  const char* now_reading;     // uppercase label on the reading stats sleep screen
  const char* last_7_days;     // uppercase label
  const char* finished_label;  // uppercase label: books finished
  const char* left_in_book;    // printf: a duration ("3 h left")
  // Library views: the cover grid, folders, pictures, finished books
  const char* library_view;    // general settings row: list or covers
  const char* view_names[2];   // list, covers
  const char* recent_label;    // uppercase label over home's cover grid
  const char* move_finished;   // general settings row
  const char* move_names[2];   // off, to the Read folder
  const char* folders;         // library tab and screen title
  const char* card_root;       // the card's top folder, under the title
  const char* folder_up;       // the row that goes up a folder
  const char* folder_empty;    // a folder with nothing to show
  const char* picture_failed;  // a picture the viewer cannot decode
  const char* hint_picture;
  // Reading extras: night mode, focus reading, book styles, links, go to
  const char* night_mode;         // reading settings row and contents row
  const char* focus_reading;      // reading settings row: the start of each word in bold
  const char* book_styles;        // reading settings row: the book's own alignment and emphasis
  const char* paragraph_indent;   // reading settings row
  const char* indent_names[3];    // as in the book, always, never
  const char* go_to;              // contents row and the panel's title
  const char* go;                 // button: go there
  const char* hint_go_coarse;     // side buttons move 10%
  const char* hint_go_fine;       // then 1%
  const char* links_on_page;      // contents row
  const char* hint_pick_link;     // choosing a link with the side buttons
  const char* back_to_page;       // contents row after following a link or going to a page
  const char* note;               // title of a footnote shown over the page
  const char* finished_question;  // at the end of a book
  const char* mark_read;          // its buttons
  const char* not_yet;
  // The first-time setup (app_setup.cpp)
  const char* setup_again;         // general settings row: runs the setup again
  const char* setup_step;          // printf: step, steps ("Step 1 of 4")
  const char* welcome;             // title of the first step
  const char* welcome_lead;        // under it: what the setup is, then the language
  const char* clock_lead;          // the clock step
  const char* menu_text_lead;      // the menu text size step
  const char* wifi_lead;           // the home Wi-Fi step
  const char* set_up_now;          // its row: opens the phone page
  const char* books_lead;          // the Add books step
  const char* open_add_books;      // its row: opens Add books
  const char* skip;                // button: on to the next step as it is
  const char* next;                // button: on to the next step
  const char* hint_welcome;        // the setup's hint
  const char* hint_welcome_first;  // on the first step, which has no way back
  // Settings sorted into pages, and the quick panel
  const char* page_settings;  // main settings row and page title: the footer, night mode, page turns
  const char* display_light;  // main settings row and page title: the light, menus, screen
  const char* sleep_battery;  // main settings row and page title
  const char* general;        // main settings row and page title: language, clock, books
  const char* light_short;    // quick panel button: the front light on or off
  const char* night_short;    // quick panel button: night mode
  const char* home_short;     // quick panel button: the home screen
  const char* sleep_short;    // quick panel button: sleep now
  const char* hint_quick;     // the quick panel's hint
  const char* starting_back;  // under "starting": the way out
  const char* back_label;     // a button that leaves the screen
  // Settings pages
  const char* add_books_over;    // Wi-Fi settings row: the network Add books uses
  const char* network_names[2];  // its values: the home Wi-Fi when set, the reader's own network
  const char* status_name;       // centred in the status bar: "Vellum" (a closing ❤ is drawn as a heart)
  // Deleting a book (held in the library: the last row of its shelves screen)
  const char* delete_book;      // that row
  const char* delete_question;  // screen title, asking first
  const char* delete_note;      // under it: what goes with the book
  const char* delete_yes;
  const char* delete_no;
  const char* delete_failed;  // a toast
  // Toasts when a book cannot be opened: DRM or a password, a PDF of
  // scanned pages, anything else.
  const char* book_locked;
  const char* book_no_text;
  const char* book_unreadable;
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

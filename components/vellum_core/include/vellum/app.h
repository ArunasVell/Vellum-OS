#pragma once

#include "vellum/battery_log.h"
#include "vellum/catalogues.h"
#include "vellum/covers.h"
#include "vellum/dictionary.h"
#include "vellum/display_queue.h"
#include "vellum/document.h"
#include "vellum/epub.h"
#include "vellum/font_library.h"
#include "vellum/hal.h"
#include "vellum/highlights.h"
#include "vellum/keyboard.h"
#include "vellum/kosync.h"
#include "vellum/library.h"
#include "vellum/opds.h"
#include "vellum/peer_transfer.h"
#include "vellum/reader.h"
#include "vellum/reading_stats.h"
#include "vellum/settings.h"
#include "vellum/shelves.h"
#include "vellum/sleep_pictures.h"
#include "vellum/strings.h"
#include "vellum/text_book.h"
#include "vellum/touch.h"
#include "vellum/updates.h"

namespace vellum {

// Event-driven app: home, library, settings and the reader. Hosts that own
// their own loop (the browser simulator) call start() once and handle() per
// input event.
//
// Controls: the side buttons (Prev/Next) move through lists and turn pages,
// Power opens the highlighted item, Home returns to the home screen. In the
// reader, Power opens the book's contents: its chapters and bookmarks, and
// a row that bookmarks the page.
// Holding Power for a second sleeps; so does leaving the reader idle for
// the time set in Settings. Any button wakes it, back where it was. When the
// battery runs out (kEmptyPercent, not charging) it sleeps with a screen
// asking to charge it. After a crash (Power::after_crash) it starts on the
// home screen with Literata, so a broken book or font cannot crash it again
// at once.
// Touch: tap an item to open it (or change a setting); in the reader, tap
// the left third or swipe right to go back, anywhere else or swipe left to
// go forward. Swipes page through lists. Swiping down from the top edge
// (or tapping the status bar of a menu) opens the quick panel
// (app_quick.cpp): the front light, night mode, and ways to the contents,
// home, Settings and sleep.
//
// Holding a word on the page looks it up in the card's dictionaries and
// offers to highlight it, or a passage ending at a word tapped next.
// Without touch, Contents › Look up a word picks a line, then a word, with
// the side buttons. Highlights are underlined on the page and listed in
// Contents; their notes are written on the phone page of "Add books".
//
// Reading time is counted while pages turn (ReadingStats): Settings ›
// Reading stats shows it by day and by book, and the speed it gives tells
// the time left in the chapter and the book (in Contents, and in the
// footer when Settings › Reading › Footer asks for it).
//
// The library lists the books by recent reading, title or author (Sort,
// kept in Settings) under tabs: All, Reading now, Finished and the reader's
// own shelves (vellum/shelves.txt), then a tab that makes a new one. Search
// opens an on-screen keyboard (Keyboard) and keeps the books whose title or
// author holds the text, accents aside. Holding a book in the list, or
// Contents › Add to shelf, opens its shelves screen: tick the shelves it is
// on, make a new one, or mark the book finished (reaching the last page
// asks "Finished the book?" once; Next on the last page asks again).
//
// Links in a book are underlined lightly (app_reading.cpp). Tapping one, or
// picking it in Contents › Links on this page, follows it; a footnote shows
// over the page instead, and after a jump Contents › Back to where you were
// returns. Contents › Go to… moves by percent of the book, and Night mode
// sets the page white on black (also in Settings › Reading, with focus
// reading, publisher styles and paragraph indents).
//
// Settings › Home and library view shows home's recent books and the
// library as a grid of covers instead of rows (app_views.cpp). When the
// card has folders, a Folders tab after Finished browses them: their
// books, and pictures, which open full screen. Settings › Move finished
// books moves a book's file to books/Read/ once it is finished and left.
//
// The library's last row, "Get free books", opens the store
// (app_store.cpp, Board::network): Project Gutenberg's and Standard
// Ebooks' catalogues, their lists, pages of books, and one book with its
// summary and a Download button. The book lands in books/ and can be read
// at once. The reader joins the home Wi-Fi for the first page it fetches
// and turns the radio off on leaving the store (or sleeping). Catalogues
// added on the phone page (Catalogues) follow the built-in ones, with
// their sections, their search and their passwords.
//
// KOReader sync (kosync.h): with an account set on the phone page, a
// book's Contents › Sync progress takes a further position from the server
// (read on another device) or sends this one. With "sync automatically"
// on, closing a book and sleeping send it too (joining the home Wi-Fi for
// a moment).
//
// Settings › Sleep screen chooses what the screen keeps while asleep
// (app_sleep.cpp): the book's cover, the reader's own pictures from the
// card's sleep/ folder (SleepPictures), a reading stats dashboard, a
// minimal page with the book's title, or the last screen as it was with a
// small badge. Its Preview row shows the sleep screen until a button is
// pressed.
//
// Settings › Device check shows every button press and touch, and what the
// board reports (battery, clock, card), for trying out new hardware.
//
// Settings › Buttons gives each press (and each held press) of Prev, Next,
// Power and Home an action, one set while reading and one in menus
// (app_controls.cpp, vellum/controls.h); the defaults are the controls
// above. Settings › Screen turns the screen upside down, or the reader on
// its side. The reader can turn pages by itself (Contents, or a button),
// and a button can save the screen to the card as a picture.
//
// A book's Contents can send it to another Vellum nearby (Board::peers);
// the other one receives while its "Add books" screen is open.
//
// "Add books" opens a Wi-Fi network and an upload page (Board::transfer);
// Home closes it and reloads the library. With a home Wi-Fi set, the page
// is served there instead (unless Settings says otherwise; a row on the
// screen switches). The page's Settings section changes the reader's
// settings, which it takes up at once.
//
// Settings › Updates looks for new versions of Vellum (Board::updater) and
// installs them, now or at night: before sleeping the reader sets a wake
// timer for kNightHour, checks then without touching the screen, and goes
// back to sleep. A version found but not installed shows as a small notice
// on the home screen. The same screen opens a phone page (the transfer
// network again) to set the home Wi-Fi or send an update file.
//
// The first time the reader starts with a memory card that Vellum has never
// used, a short setup comes before the home screen (app_setup.cpp): the
// language, the clock, the home Wi-Fi (the phone page above) and how books
// are added, each with Skip. Settings › Setup again runs it once more.
class App {
 public:
  explicit App(Board& board);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Draws the first screen with a full refresh.
  void start();
  // Handles one input event. Returns false when the app should exit.
  bool handle(const InputEvent& ev);
  // Rereads the card's books (after books were added) and shows the home screen.
  void reload_library();

  // How long the host may wait for input before calling timeout(): the
  // Power hold time while Power is down, else the idle time before sleep.
  uint32_t wait_ms() const;
  // No input for wait_ms(): sleeps (Power held, or idle long enough).
  void timeout();

 private:
  // The built-in books, when the board lists them.
  std::span<const Book* const> shipped_books() const {
    return board_.builtin_books ? builtin_books() : std::span<const Book* const>{};
  }
  enum class Screen : uint8_t {
    Home,
    Library,
    Settings,
    Reader,
    Transfer,
    Contents,
    Battery,
    Light,
    Check,
    Updates,
    Send,
    Stats,
    Store,
    Shelve,        // a book's shelves, and whether it is finished
    Folders,       // the card's folders, their books and pictures
    Picture,       // one picture, full screen
    SleepSetup,    // Settings › Sleep screen
    Pictures,      // the reader's own sleep pictures, to mark
    SleepPreview,  // the sleep screen, until a button is pressed
    Buttons,       // what each button does
    Setup          // a step of the first-time setup
  };
  // One line of the contents screen.
  enum class ContentsRow : uint8_t {
    Back,
    HomeScreen,  // closes the book, as the Home button does
    Return,      // back to the page left by a link or Go to
    Mark,
    Light,
    Night,  // night mode on or off
    GoTo,
    LookUp,
    Links,     // the links on the page, when it has some
    AutoTurn,  // starts or stops turning pages by itself
    Send,
    Shelves,  // opens the book's shelves screen
    Sync,     // KOReader sync, when an account is set
    BookmarksLabel,
    Bookmark,
    HighlightsLabel,
    Highlight,
    Note,  // under its highlight
    ChaptersLabel,
    Chapter
  };
  // Choosing words on the page, and the other panels over it (the reader
  // screen stays underneath).
  enum class WordMode : uint8_t {
    Off,
    PickLine,  // side buttons move between lines
    PickWord,  // then between words
    Look,      // a word's definition, with Highlight and Close
    Extend,    // a passage from that word: tap or move to its last word
    PickLink,  // side buttons move between the page's links (app_reading.cpp)
    Note,      // a footnote, shown like a definition
    GoTo,      // a percent of the book to go to
    Finish,    // at the end: mark the book as read?
  };
  enum class TransferState : uint8_t { Off, Running, NoCard, Failed };

  static constexpr int kSleepSetupRows = 8;   // five kinds, the pictures' order, the pictures, preview
  static constexpr int kPicturesPerPage = 6;  // thumbnails on the pictures screen
  static constexpr int kHomeRows = 2;         // other books listed under the current one
  // What the on-screen keyboard is typing for.
  enum class Typing : uint8_t { Off, Search, NewShelf, RenameShelf, NewShelfForBook, StoreSearch };

  // Home lists the current book, up to kHomeRows others, then these links.
  int home_item_count() const;
  void draw_home_buttons(int y, int first_item);
  void home_activate();
  // The library: Sort and Search, the tabs, then (on a shelf of the
  // reader's) Rename and Delete, then the books. library_sel_ counts
  // through all of them in that order.
  void open_library();
  // Rebuilds library_view_ from the tab, the order and the search.
  void build_library_view();
  int library_tab_count() const;     // All, Reading now, Finished, Folders, the shelves, New shelf
  bool library_on_shelf() const;     // a shelf of the reader's is the tab shown
  bool library_has_folders() const;  // the Folders tab is there (tab 3)
  int library_first_shelf() const;   // the tab of the reader's first shelf
  int library_first_book() const;    // selection index of the first book
  // Buttons, books, then "Get free books" (library_has_store()).
  int library_item_count() const;
  // The store link ends the All tab when there is a network; while
  // searching it ends every tab and searches the store for the same words.
  bool library_has_store() const;
  int library_rows() const;  // books on one page
  int library_list_top() const;
  void library_select_book(int view_index);
  const char* tab_name(int tab) const;
  int tab_width(int tab) const;  // px, with its padding
  // Scrolls the tabs so that `tab` is drawn.
  void library_show_tab(int tab);
  // These return true when the screen needs a redraw (false: it changed
  // screens, or drew itself).
  bool library_activate();
  bool library_key(Key key);
  bool library_gesture(const Gesture& g);
  // A book's shelves screen; `back` is where Done returns to.
  void open_shelve(int book, Screen back);
  int shelve_row_count() const;
  int shelve_rows_per_page() const;
  bool shelve_activate();
  void draw_shelve();
  void save_shelves();
  // The keyboard: opens it over the current screen, and acts on what it
  // reports. Both return true when the screen needs a redraw.
  bool open_keyboard(Typing typing, std::string_view text);
  bool keyboard_input(Keyboard::Result r);
  bool finish_typing(bool ok);
  void settings_activate();
  // Settings has a main page that leads to the others (reading, with a
  // text preview; page; display and light; sleep and battery; general).
  void open_settings(bool reading);
  void open_settings_page(int page, int sel);
  int settings_row_count() const;
  void settings_back();
  // The ‹ and a screen's title; a tap on them goes back (kBackItem).
  void draw_back_title(const char* title, int baseline);
  // Makes the menu and book typefaces from Settings current. Book fonts
  // other than Literata are rendered only when a page is set with them
  // (book_fonts()): a start that shows the home screen does not wait for them.
  void apply_fonts();
  const BookFonts& book_fonts();
  // Starts or stops receiving books over Wi-Fi (the "Add books" screen).
  // `setup` opens the phone page for home Wi-Fi and update files instead.
  void open_transfer(bool setup = false);
  void close_transfer();
  // While the network starts: takes waiting input, and says whether the
  // reader asked to leave (Home, a side button, or a tap on Back).
  bool transfer_cancel_poll();
  // A home Wi-Fi network is set (Updater::home_network).
  bool home_wifi_set() const;
  // The other network for the page: the reader's own or the home Wi-Fi.
  void switch_transfer_network();
  // The phone page saved settings or the sync account: take them up.
  void take_phone_settings();
  // An update arrived from the phone page: restart into it.
  void finish_update();
  // The first-time setup: shows step `step` (finishing after the last).
  void open_setup(int step);
  // Saves that the setup is done and shows the home screen.
  void finish_setup();
  // Home in the setup: the step before, if any.
  void setup_back();
  // These return true when the screen needs a redraw.
  bool setup_key(Key key);
  bool setup_activate();
  void draw_setup();
  // Software updates (Settings › Updates).
  void open_updates();
  void draw_updates();
  void updates_activate();
  // A newer version than the running one is known.
  bool update_available() const;
  void check_for_update();
  void install_update();
  // KOReader sync of the open book. sync_progress() first takes a further
  // position from the server (moving there) when `pull`, else (or when there
  // is none) sends this one; it shows how it went on the Contents row.
  bool can_sync() const;
  void sync_progress(bool pull);
  // Closing the book or sleeping: sends the position when the account asks
  // for it and the page moved since the last send. Leaves the radio off.
  void sync_auto();
  // The open book's id on the server; false when it has none.
  bool sync_document();
  // Shows that the new version is installed, then restarts into it.
  void restart_into_update();
  // The timer woke the reader at night: check, maybe install, sleep again.
  void night_update();
  // Before sleeping: wake at night for the update check, when it can run.
  void schedule_night_check();
  void record_update_check(UpdateResult result, const char* latest);
  // The contents screen: rows are back, bookmark toggle, bookmarks, chapters.
  void open_contents();
  int contents_row_count() const;
  ContentsRow contents_row(int i, int& index) const;
  int contents_rows_per_page() const;
  void contents_activate();
  // The contents entry (or chapter, without one) holding the current page.
  int current_chapter_row() const;
  // The bookmark on the current page, or -1.
  int page_bookmark() const;
  // Where contents entry (or chapter, without one) `index` starts.
  BookPos chapter_start(int index) const;
  // Moves the reader to `pos` in the open book and shows it.
  void jump_to(BookPos pos);
  // Rows that do nothing themselves: labels, and notes (their highlight opens).
  bool contents_skipped(int row) const;
  // The rows before the bookmarks: Back, Mark... (some only when they apply).
  int contents_actions(ContentsRow* out) const;

  // Reading extras (app_reading.cpp): links and footnotes, Go to, the
  // finish prompt. Those returning bool say whether the screen needs a redraw.
  // The link holding word i of the page (an index into the chapter's links), or -1.
  int link_at_word(int i) const;
  // The chapter's links with words on this page; returns how many.
  int page_links(int16_t* out, int max) const;
  // A tap on a link follows it; false when there is no link there.
  bool link_tap(int x, int y, bool& redraw);
  // Goes where link i leads, or shows its footnote.
  bool follow_link(int i);
  void open_link_pick();
  void open_go_to();
  void go_to_percent();
  // Asks whether the book is finished.
  void open_finish();
  bool reading_key(Key key);
  bool reading_tap(int x, int y);
  void draw_reading_panel();
  // Moves word_sel_ to the first word of link i on the page.
  void focus_link(int i);

  // Words on the page (app_words.cpp). Those returning bool say whether the
  // screen needs a redraw.
  void open_word_pick();
  bool word_hold(int x, int y);
  bool word_key(Key key);
  bool word_tap(int x, int y);
  void select_word(int i);
  void look_up_word();
  void save_highlight();
  void close_words();
  // The highlight holding word i of the page, or -1.
  int highlight_at_word(int i) const;
  // Underlines for highlights and marks for the words in hand.
  int page_marks(TextMark* out, int max) const;
  void draw_word_overlay();

  // The free book store (app_store.cpp). Each level of it is a page; Home
  // goes up one. Calls that fetch draw "Loading…" first and block.
  enum class StorePage : uint8_t { Sources, Lists, Books, Book };
  enum class StoreState : uint8_t { Ready, Loading, Downloading, Saved, Failed };
  enum class StoreError : uint8_t { None, NoNetwork, NoWifi, NoServer, BrokeOff, NotBook, CardFull, NoCard, NoEpub };
  static constexpr int kStoreEntries = 30;  // books on one page of a list (Gutenberg sends 25)
  static constexpr int kStoreHistory = 8;   // list pages "Earlier books" can go back to
  struct StoreMemory {
    OpdsParser parser;
    OpdsEntry entries[kStoreEntries];
    OpdsEntry detail;  // one book's own feed
    char history[kStoreHistory][256];
    char history_titles[kStoreHistory][64];
    Catalogues catalogues;   // the reader's own, from the card
    char auth[192];          // their sign-in header, "" for none
    char search[256];        // the search of the own catalogue shown: a template or a description
    bool search_template;    // search holds {searchTerms}
    char description[4096];  // an OpenSearch description, fetched to search
  };
  void open_store();
  // Turns the radio off; the screen stays.
  void close_store();
  void store_back();
  bool store_key(Key key);
  bool store_swipe(int forward);
  void store_activate();
  int store_row_count() const;
  bool store_busy() const;
  // Fetches a page of a list (or of search results) and shows it.
  void store_load(const char* url);
  // Shows the books matching `words`: in the reader's own catalogue being
  // browsed (its OpenSearch), else in Project Gutenberg.
  void store_search(const char* words);
  // Catalogues: the built-in ones, then the reader's own.
  int store_source_count() const;
  bool store_custom() const;  // store_source_ is one of the reader's own
  const char* store_source_name(int source) const;
  // Rows before the books on a list page: Earlier books (built-in
  // catalogues, after the first page) or Search (an own catalogue's first page).
  int store_lead_rows() const;
  int store_rows_per_page() const;
  // Remembers the list page shown, for Home (or Earlier books) to go back to.
  void store_push_history();
  // Opens entry i of the list page: its own feed when it has one.
  void store_open_book(int i);
  void store_download();
  void store_fail(StoreError e);
  static StoreError net_error(NetResult r);
  const char* store_error_text() const;
  void draw_store();
  void draw_store_book();
  // Rereads the card's books, staying on the screen shown.
  void rescan_library();

  // Buttons (app_controls.cpp). A key's press goes through Settings::buttons
  // to an action; while reading means the page shows with no words chosen
  // and no keyboard, the rest uses the menu set. Those returning bool say
  // whether the screen needs a redraw.
  bool reading_controls() const;
  bool key_down(Key key);
  bool key_up(Key key);
  bool press(int input);  // a ButtonInput
  bool act(ButtonAction a);
  // The screen's way up for `s`, and whether Prev and Next trade places.
  Rotation rotation_for(Screen s) const;
  bool keys_swapped() const;
  void open_buttons();
  void draw_buttons();
  bool buttons_activate();
  // To the start of this chapter (the previous one from its first page), or the next.
  void chapter_jump(int step);
  void toggle_bookmark();
  void toggle_light();
  void start_auto_turn();
  void stop_auto_turn();
  // The auto page turn is due: turns, stopping at the end of the book.
  void auto_turn_step();
  void take_screenshot();
  // A short notice at the bottom of the screen, gone after kToastMs.
  void show_toast(const char* text);
  void draw_toast();
  // Library views (app_views.cpp). The cover grid: home's recent books and
  // the library's books as tiles, kGridCols to a row.
  static constexpr int kGridCols = 3;
  static constexpr int kHomeTiles = 9;
  bool grid() const { return settings_.library_view == LibraryView::Covers; }
  void draw_home_grid();
  // The library's books on this page from y = top, above `bottom`.
  void draw_library_grid(int top, int bottom);
  int library_grid_rows() const;
  void draw_update_notice();
  // The folder browser: one folder's subfolders, books and pictures, with a
  // row up to its parent. Home goes up, from the top back to the library.
  enum class FolderKind : uint8_t { Up, Folder, Book, Picture };
  struct FolderItem {
    FolderKind kind;
    int16_t book;    // library index of a book
    char name[128];  // file or folder name
  };
  static constexpr int kFolderItems = 256;
  // Opens `path` ("" for the card's top) and selects the item named `select`.
  void open_folder(const char* path, const char* select = "");
  void list_folder();
  void folder_up();
  void folder_activate();
  bool folder_key(Key key);
  bool folder_gesture(const Gesture& g);
  int folder_rows() const;  // rows on one page
  // A folder item's card path into out.
  void folder_item_path(int item, char* out, size_t size) const;
  void draw_folders();
  // The picture viewer: folder item `item`, fitted to the screen. Prev and
  // Next step through the folder's pictures; Home and Power go back.
  void open_picture(int item);
  bool picture_step(int step);
  void draw_picture_screen();
  // Finished books move to books/Read/ (Settings) once the reader leaves
  // them for home, the library or the folders.
  void book_finished(int book);
  void move_finished_books();

  // The quick panel (app_quick.cpp), over the top of the screen. Those
  // returning bool say whether the screen needs a redraw.
  enum class QuickItem : uint8_t {
    Dimmer,
    Brighter,
    Cooler,
    Warmer,
    Light,
    Night,
    Contents,
    Library,
    Home,
    Settings,
    Sleep
  };
  static constexpr int kQuickItems = 10;
  static constexpr int kQuickEdge = 80;  // a swipe down starting this close to the top opens it
  bool quick_can_open() const;
  // Its buttons in order (the light's only when the device has one).
  int quick_items(QuickItem* out) const;
  void open_quick();
  void close_quick();          // and redraws the screen under it
  void close_quick_quietly();  // a screen change follows
  bool quick_activate(QuickItem item);
  bool quick_key(Key key);
  bool quick_gesture(const Gesture& g);
  void draw_quick();
  // To the home screen from anywhere the panel opens, closing the book.
  void go_home();
  // The screen itself, without the quick panel.
  void draw_screen();

  // Remembers where a tappable item was drawn; `item` is its selection index.
  void add_hit(Rect r, int item);
  int hit_at(int x, int y) const;
  bool handle_key(Key key);
  bool handle_gesture(const Gesture& g);
  // Turns a page in the reader; returns false at the start or end of the book.
  bool turn(int step);
  bool at_last_page() const;

  // Battery history: a reading now (false when the time or level is
  // unknown), and readings while in use, every BatteryLog::kIntervalS.
  bool record_battery(BatterySample::Kind kind);
  void sample_battery();
  void save_battery();

  // Book i's cover thumbnail, or nullptr when it has none (or it cannot be
  // read). Valid until the next call for the same size.
  const CoverImage* cover_of(int book, bool large);

  void open_book(int index);
  // Saves where the reader is and sleeps. `empty`: the battery ran out; the
  // sleep screen asks to charge it and no night wake is set.
  void sleep(bool empty = false);
  void wake();
  // Draws the sleep screen Settings asks for. `preview`: shown from the
  // sleep screen settings, so a shuffle does not count it as shown.
  void draw_sleep(bool empty, bool preview = false);
  // The sleep screens (app_sleep.cpp). Those returning bool draw nothing
  // and return false when they cannot be shown (the cover is drawn instead).
  void draw_sleep_cover();
  bool draw_sleep_picture(bool preview);
  void draw_sleep_stats();
  void draw_sleep_minimal();
  void draw_sleep_badge();
  // Settings › Sleep screen: the five kinds, the pictures' order, the
  // pictures, and a preview.
  void open_sleep_setup();
  void close_sleep_setup();  // back to Settings
  void draw_sleep_setup();
  void sleep_setup_activate();
  // The reader's own pictures, as thumbnails to mark.
  void open_pictures();
  static void book_converting(void* ctx, const char* path);
  void draw_pictures();
  void pictures_activate();
  // The battery is under `percent` and not charging (false when unknown).
  bool battery_below(int percent);
  void save_progress();
  void show(Screen s);  // switches screen with a full refresh
  void draw();
  void draw_home();
  void draw_library();
  void draw_settings();
  void draw_transfer();
  void draw_contents();
  void draw_battery();
  void draw_stats();
  // Reading stats: a steady count of milliseconds (the clock's when the
  // board keeps none), and the end of a stretch of reading, written out.
  uint32_t steady_ms() const;
  void stop_reading();
  // "12 min" left in the chapter and in the book at the reading speed;
  // false when the speed is not known yet.
  bool time_left(char* chapter, char* book, size_t size) const;
  // Front light: brightness and warmth, applied as they change.
  void open_light(Screen back);
  void close_light();
  void apply_light();
  void draw_light();
  // Sets the selected light slider from a tap at x; false when x is off it.
  bool light_tap(int x, int y);
  // The device check: every button and touch shows on screen, with what
  // the board reports about itself. Takes all key and touch events.
  void open_check();
  bool check_event(const InputEvent& ev);
  void draw_check();
  // Sending the open book to another reader, and receiving on "Add books".
  bool can_send() const;
  void open_send();
  void close_send();
  bool send_activate();  // false when it switched screens (and drew)
  void draw_send();
  // Hands waiting packets to the sender or receiver; with `tick`, also tells
  // them a quiet spell passed. Returns true when the screen should change.
  bool service_peers(bool tick);
  bool peers_ticking() const;
  const Strings& text() const { return strings(settings_.language); }
  // Current time in the user's format, or "" when the clock is unknown.
  void time_text(char* out, size_t size) const;

  Board& board_;
  Framebuffer fb_;
  DisplayQueue queue_;
  Library library_;
  Settings settings_;
  Epub epub_;
  char epub_path_[160] = {};  // book open in epub_
  TextBook text_book_;        // every book that is not an EPUB
  char text_path_[160] = {};  // book open in text_book_
  BuiltinDocument builtin_;
  Reader reader_;
  int turns_since_save_ = 0;
  Screen screen_ = Screen::Home;
  int reading_ = -1;  // library index of the book in reader_
  int home_sel_ = 0;
  int library_sel_ = 0;
  int library_tab_ = 0;         // 0 All, 1 Reading now, 2 Finished, 3 + k shelf k
  int library_tabs_first_ = 0;  // first tab drawn when they do not all fit
  int library_tabs_last_ = 0;   // and the last one drawn
  int library_page_ = 0;
  bool delete_armed_ = false;                  // Delete shelf was pressed once
  int library_view_[Library::kMaxBooks] = {};  // the books shown, in order
  int library_view_count_ = 0;
  char search_[Keyboard::kMaxBytes + 1] = {};         // the library's search, "" for none
  char search_before_[Keyboard::kMaxBytes + 1] = {};  // put back by Cancel
  Shelves shelves_;
  Keyboard keyboard_;
  Typing typing_ = Typing::Off;
  int shelve_book_ = 0;  // the book on the shelves screen
  int shelve_sel_ = 0;
  Screen shelve_back_ = Screen::Library;
  int settings_sel_ = 0;
  int settings_page_ = 0;  // the page of Settings shown (SettingsPage)
  FontLibrary fonts_;
  BookFonts book_fonts_ = literata_book(26);
  bool book_fonts_ready_ = false;  // book_fonts_ matches Settings
  int contents_sel_ = 0;
  int light_sel_ = 0;  // 0 brightness, 1 warmth, 2 done
  Screen light_back_ = Screen::Settings;
  int contents_marks_[Library::kMaxBookmarks] = {};  // the open book's bookmarks, in order
  int contents_mark_count_ = 0;
  // The open book's highlights as contents rows: index * 2, plus 1 for its note.
  int16_t contents_hl_rows_[2 * Highlights::kMax] = {};
  int contents_hl_row_count_ = 0;
  Highlights highlights_;
  Dictionaries dictionaries_;
  bool dictionaries_scanned_ = false;
  WordMode word_mode_ = WordMode::Off;
  int16_t page_links_[32] = {};  // PickLink: the page's links
  int page_link_count_ = 0;
  int link_pick_ = 0;            // PickLink: its place in page_links_
  int link_sel_ = 0;             // PickLink: the link in hand; Note: the link shown
  bool contents_links_ = false;  // the page had links when the contents opened
  BookPos link_back_;            // where a link or Go to left from
  int link_back_book_ = -1;      // the book it is in, or -1 for none
  int go_percent_ = 0;           // GoTo
  bool go_fine_ = false;         // GoTo: steps of 1% rather than 10%
  int finish_asked_ = -1;        // the book the finish prompt was shown for
  int word_sel_ = 0;             // the word in hand, as an index into the page's words
  int word_end_ = 0;             // Extend: the passage's last word
  int word_line_ = 0;            // PickLine: the line in hand
  int word_mark_ = -1;           // the highlight under word_sel_, or -1
  enum class Lookup : uint8_t { Found, NotFound, NoDictionary, Preparing };
  Lookup lookup_ = Lookup::NotFound;
  Dictionaries::Result lookup_result_;
  static constexpr uint32_t kDefinitionBytes = 4096;  // as much of a definition as is shown
  char* definition_;                                  // the definition shown, allocated once
  bool safe_fonts_;                                   // started after a crash: Literata until a typeface is chosen
  Rect word_overlay_;                                 // where the panel or bar was drawn
  int definition_scroll_ = 0;                         // first line shown
  int definition_lines_ = 0;                          // lines it takes, as last drawn
  int definition_rows_ = 1;                           // lines that fit in the panel
  TransferState transfer_state_ = TransferState::Off;
  bool transfer_setup_ = false;        // the phone page for updates, not books
  bool transfer_home_ = false;         // the page is on the home Wi-Fi
  bool transfer_home_failed_ = false;  // it could not be joined: the reader's own network instead
  bool transfer_cancelled_ = false;    // the reader left while it was starting
  int transfer_settings_seen_ = 0;     // TransferStatus::settings_saved taken up
  char* transfer_fonts_ = nullptr;     // TransferInfo::fonts, allocated once
  // Updates: what is known, and what this session has tried.
  enum class UpdateBusy : uint8_t { No, Checking, Installing };
  UpdateState update_state_;
  UpdateBusy update_busy_ = UpdateBusy::No;
  bool update_error_ = false;  // update_result_ is a failure to show
  UpdateResult update_result_ = UpdateResult::UpToDate;
  int install_percent_ = 0;
  int updates_sel_ = 0;
  int setup_step_ = 0;           // the setup's step shown
  int setup_sel_ = 0;            // the row or button in hand on it
  bool setup_transfer_ = false;  // the setup opened the transfer screen: back to it afterwards
  bool power_held_ = false;      // Power is down and has not acted yet
  bool power_quiet_ = false;     // and its press only stopped the pages turning
  // A side or Home button is down and its held press does something: the
  // press acts on release, or the held one after kHoldKeyMs.
  bool key_waiting_ = false;
  Key key_down_ = Key::Next;  // as pressed, before any swap
  int key_short_ = 0;         // the ButtonInputs it stands for
  int key_held_ = 0;
  uint32_t key_down_at_ = 0;  // steady_ms() when pressed
  int buttons_sel_ = 0;
  bool buttons_menus_ = false;    // the buttons screen shows the menu set
  bool buttons_refused_ = false;  // the last change found nothing safe
  bool auto_turn_ = false;        // pages turn by themselves
  uint32_t auto_turn_at_ = 0;     // steady_ms() of the next turn
  bool swallow_touch_ = false;    // a touch stopped the page turning: let it go unused
  uint8_t light_before_ = 0;      // brightness the light toggle turns back on
  char toast_[96] = {};
  uint32_t toast_at_ = 0;
  bool asleep_ = false;
  bool resume_reader_ = false;  // reopen the current book on start
  Screen before_sleep_ = Screen::Home;
  TransferInfo transfer_info_;
  CoverImage large_cover_;  // buffers for cover thumbnails, allocated once (also the pictures' thumbnails)
  CoverImage small_cover_;
  SleepPictures sleep_pictures_;
  int sleep_sel_ = 0;     // row on the sleep screen settings
  int pictures_sel_ = 0;  // picture on the pictures screen
  uint32_t sleeps_ = 0;   // varies the shuffle
  bool drawn_ = false;    // a screen was drawn since start: "Page as left" has something to keep
  BatteryLog battery_;
  ReadingStats stats_;
  uint32_t battery_saved_at_ = 0;  // time of the last sample written to the card
  struct CheckState {
    uint8_t pressed = 0;  // bit per Key: pressed at least once
    uint8_t down = 0;     // bit per Key: down now
    bool touched = false;
    bool finger = false;     // a finger is on the screen now
    int16_t x = 0, y = 0;    // last touch, logical
    int16_t px = 0, py = 0;  // the same, as the panel reports it
  } check_;
  PeerSender sender_;
  PeerReceiver receiver_;
  int send_sel_ = 0;
  bool radio_failed_ = false;  // Peers::start() failed for the send screen

  StoreMemory* store_ = nullptr;  // allocated once
  OpdsFeed store_feed_;
  StorePage store_page_ = StorePage::Sources;
  StoreState store_state_ = StoreState::Ready;
  StoreError store_error_ = StoreError::None;
  bool store_failed_download_ = false;  // Try again downloads (else it reloads)
  int store_sel_ = 0;
  int store_source_ = 0;
  int store_book_ = 0;   // the list entry the book page shows
  int store_depth_ = 0;  // pages in store_->history
  int store_percent_ = 0;
  int store_saved_ = -1;        // library index of the book just downloaded
  int store_custom_count_ = 0;  // the reader's own catalogues, as last loaded
  char store_url_[256] = {};    // the list page shown
  char store_title_[64] = {};   // its name

  // KOReader sync.
  enum class SyncState : uint8_t { Idle, Busy, Sent, Moved, UpToDate, Failed };
  kosync::Account sync_account_;
  SyncState sync_state_ = SyncState::Idle;  // shown on the Contents row
  kosync::Result sync_error_ = kosync::Result::Ok;
  int sync_percent_ = 0;         // sent, or moved to
  char sync_doc_[33] = {};       // the open book's id on the server
  uint32_t sync_doc_book_ = 0;   // reader_.id() it is for; 0 for none
  BookPos sync_sent_;            // the position last sent
  uint32_t sync_sent_book_ = 0;  // and the book, by reader_.id()

  FolderItem* folder_items_ = nullptr;  // allocated on the first visit
  int folder_count_ = 0;
  int folder_sel_ = 0;
  char folder_path_[sizeof(LibraryEntry::path)] = {};  // "" for the card's top
  int picture_item_ = -1;                              // the folder item shown
  bool picture_ready_ = false;                         // decoded into large_cover_
  bool picture_ok_ = false;
  static constexpr int kFinishedQueue = 8;
  int finished_queue_[kFinishedQueue] = {};  // library indices to move
  int finished_count_ = 0;

  struct Hit {
    Rect r;
    int item;
  };
  static constexpr int kMaxHits = 32;
  Hit hits_[kMaxHits] = {};
  int hit_count_ = 0;
  TouchTracker touch_;
  bool quick_open_ = false;
  int quick_sel_ = 0;
  uint8_t quick_light_ = 0;  // the settings as the panel opened: saved on closing if changed
  uint8_t quick_warmth_ = 0;
  bool quick_night_ = false;
};

// Blocking main loop around App. Returns only when the input source reports
// Quit (simulator); on hardware it never returns.
void run(Board& board);

}  // namespace vellum

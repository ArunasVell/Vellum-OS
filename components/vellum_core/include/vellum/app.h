#pragma once

#include "vellum/battery_log.h"
#include "vellum/covers.h"
#include "vellum/display_queue.h"
#include "vellum/document.h"
#include "vellum/epub.h"
#include "vellum/font_library.h"
#include "vellum/hal.h"
#include "vellum/library.h"
#include "vellum/peer_transfer.h"
#include "vellum/reader.h"
#include "vellum/settings.h"
#include "vellum/strings.h"
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
// the time set in Settings. Any button wakes it, back where it was.
// Touch: tap an item to open it (or change a setting); in the reader, tap
// the left third or swipe right to go back, the right third or swipe left
// to go forward, the middle for the contents. Swipes page through lists.
//
// Settings › Device check shows every button press and touch, and what the
// board reports (battery, clock, card), for trying out new hardware.
//
// A book's Contents can send it to another Vellum nearby (Board::peers);
// the other one receives while its "Add books" screen is open.
//
// "Add books" opens a Wi-Fi network and an upload page (Board::transfer);
// Home closes it and reloads the library.
//
// Settings › Updates looks for new versions of Vellum (Board::updater) and
// installs them, now or at night: before sleeping the reader sets a wake
// timer for kNightHour, checks then without touching the screen, and goes
// back to sleep. A version found but not installed shows as a small notice
// on the home screen. The same screen opens a phone page (the transfer
// network again) to set the home Wi-Fi or send an update file.
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
    Send
  };
  // One line of the contents screen.
  enum class ContentsRow : uint8_t { Back, Mark, Light, Send, BookmarksLabel, Bookmark, ChaptersLabel, Chapter };
  enum class TransferState : uint8_t { Off, Running, NoCard, Failed };

  static constexpr int kHomeRows = 2;     // other books listed under the current one
  static constexpr int kLibraryRows = 7;  // books per library page

  // Home lists the current book, up to kHomeRows others, then these links.
  int home_item_count() const;
  void home_activate();
  void library_activate();
  void settings_activate();
  // Settings has a general page and a reading page (with a text preview).
  void open_settings(bool reading);
  // Makes the menu and book typefaces from Settings current.
  void apply_fonts();
  // Starts or stops receiving books over Wi-Fi (the "Add books" screen).
  // `setup` opens the phone page for home Wi-Fi and update files instead.
  void open_transfer(bool setup = false);
  void close_transfer();
  // An update arrived from the phone page: restart into it.
  void finish_update();
  // Software updates (Settings › Updates).
  void open_updates();
  void draw_updates();
  void updates_activate();
  // A newer version than the running one is known.
  bool update_available() const;
  void check_for_update();
  void install_update();
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
  // Moves the reader to `pos` in the open book and shows it.
  void jump_to(BookPos pos);

  // Remembers where a tappable item was drawn; `item` is its selection index.
  void add_hit(Rect r, int item);
  int hit_at(int x, int y) const;
  bool handle_key(Key key);
  bool handle_gesture(const Gesture& g);
  // Turns a page in the reader; returns false at the start or end of the book.
  bool turn(int step);

  // Battery history: a reading now (false when the time or level is
  // unknown), and readings while in use, every BatteryLog::kIntervalS.
  bool record_battery(BatterySample::Kind kind);
  void sample_battery();
  void save_battery();

  // Book i's cover thumbnail, or nullptr when it has none (or it cannot be
  // read). Valid until the next call for the same size.
  const CoverImage* cover_of(int book, bool large);

  void open_book(int index);
  void sleep();
  void wake();
  void draw_sleep();
  void save_progress();
  void show(Screen s);  // switches screen with a full refresh
  void draw();
  void draw_home();
  void draw_library();
  void draw_settings();
  void draw_transfer();
  void draw_contents();
  void draw_battery();
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
  BuiltinDocument builtin_;
  Reader reader_;
  int turns_since_save_ = 0;
  Screen screen_ = Screen::Home;
  int reading_ = -1;  // library index of the book in reader_
  int home_sel_ = 0;
  int library_sel_ = 0;
  int settings_sel_ = 0;
  bool settings_reading_ = false;  // the reading page of Settings
  FontLibrary fonts_;
  BookFonts book_fonts_ = literata_book(26);
  int contents_sel_ = 0;
  int light_sel_ = 0;  // 0 brightness, 1 warmth, 2 done
  Screen light_back_ = Screen::Settings;
  int contents_marks_[Library::kMaxBookmarks] = {};  // the open book's bookmarks, in order
  int contents_mark_count_ = 0;
  TransferState transfer_state_ = TransferState::Off;
  bool transfer_setup_ = false;  // the phone page for updates, not books
  // Updates: what is known, and what this session has tried.
  enum class UpdateBusy : uint8_t { No, Checking, Installing };
  UpdateState update_state_;
  UpdateBusy update_busy_ = UpdateBusy::No;
  bool update_error_ = false;  // update_result_ is a failure to show
  UpdateResult update_result_ = UpdateResult::UpToDate;
  int install_percent_ = 0;
  int updates_sel_ = 0;
  bool power_held_ = false;  // Power is down and has not acted yet
  bool asleep_ = false;
  bool resume_reader_ = false;  // reopen the current book on start
  Screen before_sleep_ = Screen::Home;
  TransferInfo transfer_info_;
  CoverImage large_cover_;  // buffers for cover thumbnails, allocated once
  CoverImage small_cover_;
  BatteryLog battery_;
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

  struct Hit {
    Rect r;
    int item;
  };
  static constexpr int kMaxHits = 16;
  Hit hits_[kMaxHits] = {};
  int hit_count_ = 0;
  TouchTracker touch_;
};

// Blocking main loop around App. Returns only when the input source reports
// Quit (simulator); on hardware it never returns.
void run(Board& board);

}  // namespace vellum

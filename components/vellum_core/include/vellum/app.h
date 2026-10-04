#pragma once

#include "vellum/display_queue.h"
#include "vellum/document.h"
#include "vellum/epub.h"
#include "vellum/hal.h"
#include "vellum/library.h"
#include "vellum/reader.h"
#include "vellum/settings.h"
#include "vellum/strings.h"
#include "vellum/touch.h"

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
// "Add books" opens a Wi-Fi network and an upload page (Board::transfer);
// Home closes it and reloads the library.
class App {
 public:
  explicit App(Board& board);

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
  enum class Screen : uint8_t { Home, Library, Settings, Reader, Transfer, Contents };
  // One line of the contents screen.
  enum class ContentsRow : uint8_t { Back, Mark, BookmarksLabel, Bookmark, ChaptersLabel, Chapter };
  enum class TransferState : uint8_t { Off, Running, NoCard, Failed };

  static constexpr int kHomeRows = 2;     // other books listed under the current one
  static constexpr int kLibraryRows = 7;  // books per library page

  // Home lists the current book, up to kHomeRows others, then these links.
  int home_item_count() const;
  void home_activate();
  void library_activate();
  void settings_activate();
  // Starts or stops receiving books over Wi-Fi (the "Add books" screen).
  void open_transfer();
  void close_transfer();
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
  int contents_sel_ = 0;
  int contents_marks_[Library::kMaxBookmarks] = {};  // the open book's bookmarks, in order
  int contents_mark_count_ = 0;
  TransferState transfer_state_ = TransferState::Off;
  bool power_held_ = false;  // Power is down and has not acted yet
  bool asleep_ = false;
  bool resume_reader_ = false;  // reopen the current book on start
  Screen before_sleep_ = Screen::Home;
  TransferInfo transfer_info_;

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

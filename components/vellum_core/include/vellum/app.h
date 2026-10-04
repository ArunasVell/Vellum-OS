#pragma once

#include "vellum/display_queue.h"
#include "vellum/hal.h"
#include "vellum/library.h"
#include "vellum/reader.h"
#include "vellum/settings.h"
#include "vellum/strings.h"

namespace vellum {

// Event-driven app: home, library, settings and the reader. Hosts that own
// their own loop (the browser simulator) call start() once and handle() per
// input event.
//
// Controls: the side buttons (Prev/Next) move through lists and turn pages,
// Power opens the highlighted item, Home returns to the home screen.
class App {
 public:
  explicit App(Board& board);

  // Draws the first screen with a full refresh.
  void start();
  // Handles one input event. Returns false when the app should exit.
  bool handle(const InputEvent& ev);

 private:
  enum class Screen : uint8_t { Home, Library, Settings, Reader };

  static constexpr int kHomeRows = 3;     // other books listed under the current one
  static constexpr int kLibraryRows = 7;  // books per library page

  // Home lists the current book, up to kHomeRows others, then these links.
  int home_item_count() const;
  void home_activate();
  void library_activate();
  void settings_activate();

  void open_book(int index);
  void show(Screen s);  // switches screen with a full refresh
  void draw();
  void draw_home();
  void draw_library();
  void draw_settings();
  const Strings& text() const { return strings(settings_.language); }
  // Current time in the user's format, or "" when the clock is unknown.
  void time_text(char* out, size_t size) const;

  Board& board_;
  Framebuffer fb_;
  DisplayQueue queue_;
  Library library_;
  Settings settings_;
  Reader reader_;
  Screen screen_ = Screen::Home;
  int reading_ = -1;  // library index of the book in reader_
  int home_sel_ = 0;
  int library_sel_ = 0;
  int settings_sel_ = 0;
};

// Blocking main loop around App. Returns only when the input source reports
// Quit (simulator); on hardware it never returns.
void run(Board& board);

}  // namespace vellum

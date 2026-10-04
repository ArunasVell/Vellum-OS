#pragma once

#include "vellum/book.h"
#include "vellum/display_queue.h"
#include "vellum/hal.h"
#include "vellum/text.h"

namespace vellum {

// Event-driven reader app. Hosts that own their own loop (the browser
// simulator) call start() once and handle() per input event.
class App {
 public:
  explicit App(Board& board);

  // Draws the first screen with a full refresh.
  void start();
  // Handles one input event. Returns false when the app should exit.
  bool handle(const InputEvent& ev);

 private:
  static constexpr int kMaxPages = 512;

  void paginate();
  void draw_page();

  Board& board_;
  Framebuffer fb_;
  DisplayQueue queue_;
  const Book& book_;
  TextPos page_starts_[kMaxPages];
  int page_count_ = 0;
  int page_ = 0;
};

// Blocking main loop around App. Returns only when the input source reports
// Quit (simulator); on hardware it never returns.
void run(Board& board);

}  // namespace vellum

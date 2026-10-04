#pragma once

#include "vellum/display_queue.h"
#include "vellum/hal.h"

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
  Board& board_;
  Framebuffer fb_;
  DisplayQueue queue_;
  int page_ = 0;
};

// Blocking main loop around App. Returns only when the input source reports
// Quit (simulator); on hardware it never returns.
void run(Board& board);

}  // namespace vellum

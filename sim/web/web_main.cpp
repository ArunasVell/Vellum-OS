// Browser simulator glue: exposes the core app to JavaScript.
//
// The page owns the event loop. It calls vellum_start() once, vellum_key()
// per button press, and after each call reads the latest frame when
// vellum_frame_count() has changed. vellum_last_full() tells it to play the
// e-ink flash for a full refresh.
#include <emscripten/emscripten.h>

#include <cstdint>
#include <ctime>
#include <vector>

#include "vellum/app.h"
#include "vellum/stdio_storage.h"

namespace {

constexpr int kPanelW = 800;
constexpr int kPanelH = 480;

class WebDisplay : public vellum::DisplayPanel {
 public:
  int width() const override { return kPanelW; }
  int height() const override { return kPanelH; }

  void present(const vellum::Framebuffer& fb, vellum::RefreshMode mode, vellum::Rect) override {
    w_ = fb.width();
    h_ = fb.height();
    rgba_.resize(static_cast<size_t>(w_) * h_ * 4);
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        uint8_t* p = &rgba_[(static_cast<size_t>(y) * w_ + x) * 4];
        const bool white = fb.pixel(x, y) == vellum::Color::White;
        // E-ink tones: warm paper and soft ink rather than pure white/black.
        p[0] = white ? 0xEC : 0x24;
        p[1] = white ? 0xEA : 0x24;
        p[2] = white ? 0xE4 : 0x27;
        p[3] = 0xFF;
      }
    }
    last_full_ = mode == vellum::RefreshMode::Full;
    ++frames_;
  }

  std::vector<uint8_t> rgba_;
  int w_ = 0;
  int h_ = 0;
  int frames_ = 0;
  bool last_full_ = false;
};

class NoInput : public vellum::InputSource {
 public:
  bool wait(vellum::InputEvent&, uint32_t) override { return false; }
};

class WebPower : public vellum::Power {
 public:
  int battery_percent() override { return percent; }
  bool charging() override { return false; }
  int percent = 82;
};

// The browser's local time.
class WebClock : public vellum::Clock {
 public:
  bool now(vellum::DateTime& out) override {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) return false;
    out = {static_cast<uint16_t>(local.tm_year + 1900), static_cast<uint8_t>(local.tm_mon + 1),
           static_cast<uint8_t>(local.tm_mday),         static_cast<uint8_t>(local.tm_hour),
           static_cast<uint8_t>(local.tm_min),          static_cast<uint8_t>(local.tm_sec)};
    return true;
  }
};

WebDisplay display;
NoInput input;
WebPower power;
WebClock clock;
// The page writes dropped EPUBs into /card/books in Emscripten's in-memory file system.
vellum::StdioStorage card("/card");
std::vector<uint8_t> framebuffer(vellum::Framebuffer::bytes_for(kPanelW, kPanelH));
vellum::Board board{"Browser simulator",   display,           input, power, nullptr, &clock, &card,
                    vellum::Rotation::R90, framebuffer.data()};
vellum::App* app = nullptr;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void vellum_start(int battery) {
  power.percent = battery;
  static vellum::App instance(board);
  app = &instance;
  app->start();
}

// 0 = previous, 1 = next, 2 = home, 3 = power
EMSCRIPTEN_KEEPALIVE void vellum_key(int key) {
  if (!app || key < 0 || key > 3) return;
  vellum::InputEvent ev;
  ev.type = vellum::InputEvent::Type::KeyDown;
  ev.key = static_cast<vellum::Key>(key);
  app->handle(ev);
}

// Call after writing books into /card/books.
EMSCRIPTEN_KEEPALIVE void vellum_reload_library() {
  if (app) app->reload_library();
}

EMSCRIPTEN_KEEPALIVE int vellum_frame_count() { return display.frames_; }
EMSCRIPTEN_KEEPALIVE int vellum_last_full() { return display.last_full_ ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int vellum_width() { return display.w_; }
EMSCRIPTEN_KEEPALIVE int vellum_height() { return display.h_; }
EMSCRIPTEN_KEEPALIVE const uint8_t* vellum_pixels() { return display.rgba_.data(); }
}

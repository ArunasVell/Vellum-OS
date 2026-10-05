// Desktop simulator: a board profile backed by an SDL window.
//
// Mimics the X4 Pro's 800x480 landscape panel shown in portrait. A full
// refresh briefly flashes the screen inverted, like e-ink does.
//
// Keys: Right/Space/PageDown = next, Left/PageUp = prev, H = home,
// Enter/P = power (opens the highlighted item), Esc/Q = quit. The mouse is
// the touch screen: click to tap, drag to swipe, hold still to hold a word.
//
// Headless (for CI and screenshots):
//   vellum-sim --headless --keys onnp --time 14:05 --screenshot page.png
// where n = next, p = prev, h = home, o = power, z = hold power (sleep),
// [X,Y] taps the logical
// point (X,Y), [X,Y~] holds a finger there (a long press) and [X,Y>X2,Y2]
// swipes from one point to the other.
// --time fixes the clock.
// --card DIR uses a folder as the SD card: EPUBs go in DIR/books.
// --demo-store serves the free book store's canned demo catalogue even when
// the simulator was built with libcurl (which reaches the real catalogues).
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "demo_network.h"
#include "demo_updater.h"
#include "png_writer.h"
#include "vellum/app.h"
#include "vellum/stdio_storage.h"
#ifdef VELLUM_SIM_CURL
#include "curl_network.h"
#endif

namespace {

constexpr int kPanelW = 800;
constexpr int kPanelH = 480;
constexpr int kFlashMs = 150;

struct Options {
  bool headless = false;
  std::string keys;
  std::string screenshot;
  int scale = 1;
  int battery = 82;
  int fixed_minutes = -1;   // --time HH:MM; -1 = the computer's clock
  std::string card;         // --card DIR: a folder that plays the SD card
  bool demo_store = false;  // --demo-store
};

const char* mode_name(vellum::RefreshMode m) { return m == vellum::RefreshMode::Full ? "full" : "fast"; }

class SimDisplay : public vellum::DisplayPanel {
 public:
  explicit SimDisplay(const Options& opt) : opt_(opt) {}

  bool open() {
    if (opt_.headless) return true;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
      return false;
    }
    // Portrait window: the panel's long side is vertical when reading.
    window_ = SDL_CreateWindow("Vellum simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kPanelH * opt_.scale,
                               kPanelW * opt_.scale, 0);
    renderer_ = window_ ? SDL_CreateRenderer(window_, -1, 0) : nullptr;
    texture_ = renderer_
                   ? SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, kPanelH, kPanelW)
                   : nullptr;
    if (!texture_) {
      std::fprintf(stderr, "SDL window setup failed: %s\n", SDL_GetError());
      return false;
    }
    return true;
  }

  ~SimDisplay() override {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (!opt_.headless) SDL_Quit();
  }

  int width() const override { return kPanelW; }
  int height() const override { return kPanelH; }

  void present(const vellum::Framebuffer& fb, vellum::RefreshMode mode, vellum::Rect dirty) override {
    ++count_;
    std::printf("present #%d %s dirty=%d,%d %dx%d\n", count_, mode_name(mode), dirty.x, dirty.y, dirty.w, dirty.h);
    capture(fb);
    if (opt_.headless) return;
    if (mode == vellum::RefreshMode::Full) {
      show(true);
      SDL_Delay(kFlashMs);
    }
    show(false);
  }

  bool save_screenshot(const std::string& path) const { return sim::write_gray_png(path, shot_w_, shot_h_, gray_); }

 private:
  // Copies the logical (rotated) image the reader sees.
  void capture(const vellum::Framebuffer& fb) {
    shot_w_ = fb.width();
    shot_h_ = fb.height();
    gray_.resize(static_cast<size_t>(shot_w_) * shot_h_);
    for (int y = 0; y < shot_h_; ++y)
      for (int x = 0; x < shot_w_; ++x)
        gray_[static_cast<size_t>(y) * shot_w_ + x] = fb.pixel(x, y) == vellum::Color::White ? 0xF4 : 0x1C;
  }

  void show(bool inverted) {
    void* pixels;
    int pitch;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) != 0) return;
    for (int y = 0; y < shot_h_; ++y) {
      auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + y * pitch);
      for (int x = 0; x < shot_w_; ++x) {
        uint8_t v = gray_[static_cast<size_t>(y) * shot_w_ + x];
        if (inverted) v = static_cast<uint8_t>(0xFF - v);
        row[x] = (uint32_t{v} << 16) | (uint32_t{v} << 8) | v;
      }
    }
    SDL_UnlockTexture(texture_);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
  }

  const Options& opt_;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  std::vector<uint8_t> gray_;
  int shot_w_ = 0;
  int shot_h_ = 0;
  int count_ = 0;
};

class SimInput : public vellum::InputSource {
 public:
  explicit SimInput(const Options& opt) : opt_(opt) {}

  bool wait(vellum::InputEvent& out, uint32_t timeout_ms) override {
    return opt_.headless ? next_scripted(out) : next_sdl(out, timeout_ms);
  }
  uint32_t now_ms() override {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
  }

 private:
  // A touch event at logical point (x, y) on the portrait screen.
  static vellum::InputEvent touch(vellum::InputEvent::Type type, int x, int y, uint32_t ms = 0) {
    vellum::InputEvent ev;
    ev.type = type;
    ev.ms = ms;
    int px, py;
    vellum::rotate_to_physical(vellum::Rotation::R90, kPanelW, kPanelH, x, y, px, py);
    ev.x = static_cast<int16_t>(std::clamp(px, 0, kPanelW - 1));
    ev.y = static_cast<int16_t>(std::clamp(py, 0, kPanelH - 1));
    return ev;
  }

  // Parses "[X,Y]", "[X,Y~]" or "[X,Y>X2,Y2]" at pos_ (just after the '[') into touch events.
  void script_touch() {
    using T = vellum::InputEvent::Type;
    const size_t end = opt_.keys.find(']', pos_);
    if (end == std::string::npos) {
      pos_ = opt_.keys.size();
      return;
    }
    int x0, y0, x1, y1;
    const std::string spec = opt_.keys.substr(pos_, end - pos_);
    pos_ = end + 1;
    const int n = std::sscanf(spec.c_str(), "%d,%d>%d,%d", &x0, &y0, &x1, &y1);
    if (n < 2) return;
    if (n < 4) {
      x1 = x0;
      y1 = y0;
    }
    pending_.push_back(touch(T::TouchDown, x0, y0));
    if (n < 4 && spec.find('~') != std::string::npos) pending_.push_back(std::nullopt);  // a wait that times out: held
    if (n == 4) pending_.push_back(touch(T::TouchMove, (x0 + x1) / 2, (y0 + y1) / 2));
    pending_.push_back(touch(T::TouchUp, x1, y1));
  }

  bool next_scripted(vellum::InputEvent& out) {
    using T = vellum::InputEvent::Type;
    for (;;) {
      if (!pending_.empty()) {
        const std::optional<vellum::InputEvent> next = pending_.front();
        pending_.pop_front();
        if (!next) return false;
        out = *next;
        return true;
      }
      if (pos_ >= opt_.keys.size()) break;
      const char c = opt_.keys[pos_++];
      out.type = T::KeyDown;
      switch (c) {
        case '[':
          script_touch();
          break;
        case 'n':
          out.key = vellum::Key::Next;
          return true;
        case 'p':
          out.key = vellum::Key::Prev;
          return true;
        case 'h':
          out.key = vellum::Key::Home;
          return true;
        case 'o':
        case 'z': {
          out.key = vellum::Key::Power;
          vellum::InputEvent up = out;
          up.type = T::KeyUp;
          if (c == 'z') pending_.push_back(std::nullopt);  // a wait that times out: held
          pending_.push_back(up);
          return true;
        }
        default:
          break;  // ignore unknown characters
      }
    }
    out.type = T::Quit;
    return true;
  }

  bool next_sdl(vellum::InputEvent& out, uint32_t timeout_ms) {
    using T = vellum::InputEvent::Type;
    SDL_Event e;
    const int ms = timeout_ms > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(timeout_ms);
    if (!SDL_WaitEventTimeout(&e, ms)) return false;
    if (e.type == SDL_QUIT) {
      out.type = T::Quit;
      return true;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || (e.type == SDL_MOUSEMOTION && mouse_down_)) {
      if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button != SDL_BUTTON_LEFT) {
          out.type = T::None;
          return true;
        }
        mouse_down_ = e.type == SDL_MOUSEBUTTONDOWN;
      }
      const int x = (e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x) / opt_.scale;
      const int y = (e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y) / opt_.scale;
      const T type = e.type == SDL_MOUSEMOTION       ? T::TouchMove
                     : e.type == SDL_MOUSEBUTTONDOWN ? T::TouchDown
                                                     : T::TouchUp;
      out = touch(type, x, y, e.type == SDL_MOUSEMOTION ? e.motion.timestamp : e.button.timestamp);
      return true;
    }
    if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP) {
      out.type = T::None;
      return true;
    }
    out.type = e.type == SDL_KEYDOWN ? T::KeyDown : T::KeyUp;
    switch (e.key.keysym.sym) {
      case SDLK_RIGHT:
      case SDLK_SPACE:
      case SDLK_PAGEDOWN:
        out.key = vellum::Key::Next;
        break;
      case SDLK_LEFT:
      case SDLK_PAGEUP:
        out.key = vellum::Key::Prev;
        break;
      case SDLK_h:
        out.key = vellum::Key::Home;
        break;
      case SDLK_p:
      case SDLK_RETURN:
        out.key = vellum::Key::Power;
        break;
      case SDLK_ESCAPE:
      case SDLK_q:
        out.type = T::Quit;
        break;
      default:
        out.type = T::None;
        break;
    }
    return true;
  }

  const Options& opt_;
  size_t pos_ = 0;
  std::deque<std::optional<vellum::InputEvent>> pending_;  // nullopt: a timeout
  bool mouse_down_ = false;
};

class SimPower : public vellum::Power {
 public:
  explicit SimPower(int percent) : percent_(percent) {}
  int battery_percent() override { return percent_; }
  bool charging() override { return false; }

 private:
  int percent_;
};

class SimClock : public vellum::Clock {
 public:
  explicit SimClock(const Options& opt) : opt_(opt) {}

  bool now(vellum::DateTime& out) override {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) return false;
    out = {static_cast<uint16_t>(local.tm_year + 1900), static_cast<uint8_t>(local.tm_mon + 1),
           static_cast<uint8_t>(local.tm_mday),         static_cast<uint8_t>(local.tm_hour),
           static_cast<uint8_t>(local.tm_min),          static_cast<uint8_t>(local.tm_sec)};
    if (opt_.fixed_minutes >= 0) {
      out.hour = static_cast<uint8_t>(opt_.fixed_minutes / 60);
      out.minute = static_cast<uint8_t>(opt_.fixed_minutes % 60);
      out.second = 0;
    }
    return true;
  }

 private:
  const Options& opt_;
};

// Prints front light changes; the window does not glow.
class SimFrontlight : public vellum::Frontlight {
 public:
  void set(uint8_t level, uint8_t warmth) override {
    if (level == level_ && warmth == warmth_) return;
    level_ = level;
    warmth_ = warmth;
    std::printf("front light %d/255, warmth %d/255\n", level, warmth);
  }

 private:
  int level_ = -1;
  int warmth_ = -1;
};

// Shows the "Add books" screen as the device would; there is no real
// network here. Copy books into the card folder's books/ instead.
class SimTransfer : public vellum::Transfer {
 public:
  bool start(vellum::TransferInfo& info) override {
    std::snprintf(info.network, sizeof(info.network), "Vellum-SIM");
    std::snprintf(info.password, sizeof(info.password), "48151623");
    std::snprintf(info.address, sizeof(info.address), "http://192.168.4.1");
    return true;
  }
  void stop() override {}
  void status(vellum::TransferStatus& out) override { out = {}; }
};

bool parse(int argc, char** argv, Options& opt) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    const char* v = nullptr;
    if (a == "--headless") {
      opt.headless = true;
    } else if (a == "--keys" && (v = value())) {
      opt.keys = v;
    } else if (a == "--screenshot" && (v = value())) {
      opt.screenshot = v;
    } else if (a == "--scale" && (v = value())) {
      opt.scale = std::max(1, std::atoi(v));
    } else if (a == "--battery" && (v = value())) {
      opt.battery = std::atoi(v);
    } else if (a == "--demo-store") {
      opt.demo_store = true;
    } else if (a == "--card" && (v = value())) {
      opt.card = v;
    } else if (a == "--time" && (v = value())) {
      int h = 0, m = 0;
      if (std::sscanf(v, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
      opt.fixed_minutes = h * 60 + m;
    } else {
      std::fprintf(stderr,
                   "usage: vellum-sim [--headless] [--keys onph] [--screenshot out.png] [--scale N] [--battery N] "
                   "[--time HH:MM] [--card DIR] [--demo-store]\n");
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse(argc, argv, opt)) return 2;

  SimDisplay display(opt);
  if (!display.open()) return 1;
  SimInput input(opt);
  SimPower power(opt.battery);
  SimClock clock(opt);
  SimFrontlight light;
  // Static so it outlives the app run() keeps, which may still hold a book open at exit.
  static vellum::StdioStorage card(opt.card.c_str());
  SimTransfer transfer;
  std::vector<uint8_t> framebuffer(vellum::Framebuffer::bytes_for(kPanelW, kPanelH));

  vellum::Board board{"Simulator (X4 Pro geometry)",
                      display,
                      input,
                      power,
                      &light,
                      &clock,
                      opt.card.empty() ? nullptr : &card,
                      vellum::Rotation::R90,
                      framebuffer.data(),
                      &transfer};
  DemoUpdater updater;
  board.version = "sim";
  board.updater = &updater;
  DemoNetwork demo_network(VELLUM_DEMO_DATA);
  board.network = &demo_network;
#ifdef VELLUM_SIM_CURL
  CurlNetwork internet;
  if (!opt.demo_store) board.network = &internet;
#endif
  vellum::run(board);

  if (!opt.screenshot.empty() && !display.save_screenshot(opt.screenshot)) {
    std::fprintf(stderr, "could not write %s\n", opt.screenshot.c_str());
    return 1;
  }
  return 0;
}

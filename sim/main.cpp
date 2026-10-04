// Desktop simulator: a board profile backed by an SDL window.
//
// Mimics the X4 Pro's 800x480 landscape panel shown in portrait. A full
// refresh briefly flashes the screen inverted, like e-ink does.
//
// Keys: Right/Space/PageDown = next, Left/PageUp = prev, H = home (full
// refresh), P = power, Esc/Q = quit.
//
// Headless (for CI and screenshots):
//   vellum-sim --headless --keys nnnp --screenshot page.png
// where n = next, p = prev, h = home.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "png_writer.h"
#include "vellum/app.h"

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

 private:
  bool next_scripted(vellum::InputEvent& out) {
    using T = vellum::InputEvent::Type;
    while (pos_ < opt_.keys.size()) {
      const char c = opt_.keys[pos_++];
      out.type = T::KeyDown;
      switch (c) {
        case 'n':
          out.key = vellum::Key::Next;
          return true;
        case 'p':
          out.key = vellum::Key::Prev;
          return true;
        case 'h':
          out.key = vellum::Key::Home;
          return true;
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
};

class SimPower : public vellum::Power {
 public:
  explicit SimPower(int percent) : percent_(percent) {}
  int battery_percent() override { return percent_; }
  bool charging() override { return false; }

 private:
  int percent_;
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
    } else {
      std::fprintf(stderr,
                   "usage: vellum-sim [--headless] [--keys npnh] [--screenshot out.png] [--scale N] [--battery N]\n");
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
  std::vector<uint8_t> framebuffer(vellum::Framebuffer::bytes_for(kPanelW, kPanelH));

  vellum::Board board{"Simulator (X4 Pro geometry)", display,           input, power, nullptr,
                      vellum::Rotation::R90,         framebuffer.data()};
  vellum::run(board);

  if (!opt.screenshot.empty() && !display.save_screenshot(opt.screenshot)) {
    std::fprintf(stderr, "could not write %s\n", opt.screenshot.c_str());
    return 1;
  }
  return 0;
}

// The quick panel: a sheet that slides down over the top of any screen, as
// on a phone. Swiping down from the top edge opens it (as does tapping a
// menu's status bar, or a button set to Quick panel). It holds the time and
// battery, the front light's brightness and warmth with − and + buttons,
// and big buttons: the light on or off, night mode, the book's contents (or
// the library when no book is open), home, Settings and sleep.
//
// While it is open it takes every press: the side buttons move between its
// buttons, Power presses one, Home closes it. A tap under it or a swipe up
// closes it too.
#include <algorithm>
#include <cstdio>

#include "vellum/app.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

constexpr int kSliderTop = 66;     // first slider block, under the status line
constexpr int kSliderBlock = 104;  // name and value, then − track +
constexpr int kRound = 56;         // the − and + buttons
constexpr int kKnob = 18;
constexpr int kTileH = 74;
constexpr int kTileGap = 10;
constexpr int kTileCols = 3;
constexpr int kTrackItem = 100;  // hit items of the two tracks: 100 brightness, 101 warmth
constexpr int kOutsideItem = 200;

// A rounded outlined box, filled black under the cursor. Returns the text colour.
Color draw_box(Framebuffer& fb, Rect r, int radius, bool selected) {
  fb.fill_round_rect(r, radius, Color::Black);
  if (!selected) fb.fill_round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, radius - 2, Color::White);
  return selected ? Color::White : Color::Black;
}

void draw_sign(Framebuffer& fb, Rect r, bool plus, Color c) {
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;
  fb.fill_rect({cx - 11, cy - 2, 22, 4}, c);
  if (plus) fb.fill_rect({cx - 2, cy - 11, 4, 22}, c);
}

int baseline_in(Rect r, const Font& font) { return r.y + (r.h + font.ascent * 7 / 10) / 2; }

}  // namespace

bool App::quick_can_open() const {
  if (asleep_ || typing_ != Typing::Off) return false;
  switch (screen_) {
    case Screen::Reader:
      return word_mode_ == WordMode::Off;
    case Screen::Updates:
      return update_busy_ == UpdateBusy::No;
    // Screens with their own way out, or busy with the radio.
    case Screen::Setup:
    case Screen::Check:
    case Screen::SleepPreview:
    case Screen::Transfer:
    case Screen::Store:
    case Screen::Send:
      return false;
    default:
      return true;
  }
}

int App::quick_items(QuickItem* out) const {
  int n = 0;
  if (board_.frontlight != nullptr) {
    out[n++] = QuickItem::Dimmer;
    out[n++] = QuickItem::Brighter;
    out[n++] = QuickItem::Cooler;
    out[n++] = QuickItem::Warmer;
    out[n++] = QuickItem::Light;
  }
  out[n++] = QuickItem::Night;
  out[n++] = reading_ >= 0 && (screen_ == Screen::Reader || screen_ == Screen::Contents) ? QuickItem::Contents
                                                                                         : QuickItem::Library;
  out[n++] = QuickItem::Home;
  out[n++] = QuickItem::Settings;
  out[n++] = QuickItem::Sleep;
  return n;
}

void App::open_quick() {
  if (auto_turn_) stop_auto_turn();
  quick_open_ = true;
  quick_light_ = settings_.light;
  quick_warmth_ = settings_.warmth;
  quick_night_ = settings_.night;
  QuickItem items[kQuickItems];
  const int n = quick_items(items);
  // On the first big button, the light's when there is one.
  quick_sel_ = 0;
  for (int i = 0; i < n; ++i) {
    if (items[i] == QuickItem::Light || items[i] == QuickItem::Night) {
      quick_sel_ = i;
      break;
    }
  }
  draw();
  queue_.submit();
}

void App::close_quick() {
  close_quick_quietly();
  draw();
  queue_.submit_full();  // the panel leaves no ghost
}

// Closes the panel without drawing: a screen change follows.
void App::close_quick_quietly() {
  quick_open_ = false;
  if (board_.storage != nullptr &&
      (settings_.light != quick_light_ || settings_.warmth != quick_warmth_ || settings_.night != quick_night_)) {
    save_settings(*board_.storage, settings_);
  }
}

void App::go_home() {
  const bool closing_book = screen_ == Screen::Reader || screen_ == Screen::Contents;
  if (closing_book) save_progress();
  if (screen_ == Screen::Light && board_.storage != nullptr) save_settings(*board_.storage, settings_);
  show(Screen::Home);
  if (closing_book) sync_auto();  // the home screen shows meanwhile
}

bool App::quick_activate(QuickItem item) {
  using Q = QuickItem;
  switch (item) {
    case Q::Dimmer:
    case Q::Brighter:
    case Q::Cooler:
    case Q::Warmer: {
      const bool warmth = item == Q::Cooler || item == Q::Warmer;
      uint8_t& v = warmth ? settings_.warmth : settings_.light;
      v = static_cast<uint8_t>(std::clamp(v + (item == Q::Brighter || item == Q::Warmer ? 1 : -1), 0, kLightSteps));
      apply_light();
      return true;
    }
    case Q::Light:
      if (settings_.light > 0) {
        light_before_ = settings_.light;
        settings_.light = 0;
      } else {
        settings_.light = light_before_ > 0 ? light_before_ : kLightSteps / 2;
      }
      apply_light();
      return true;
    case Q::Night:
      settings_.night = !settings_.night;
      if (screen_ == Screen::Reader) queue_.request_full();  // the whole page turns over
      return true;
    case Q::Contents:
      close_quick_quietly();
      open_contents();
      return false;
    case Q::Library:
      close_quick_quietly();
      if (screen_ == Screen::Reader || screen_ == Screen::Contents) save_progress();
      open_library();
      return false;
    case Q::Home:
      close_quick_quietly();
      go_home();
      return false;
    case Q::Settings:
      close_quick_quietly();
      if (screen_ == Screen::Reader || screen_ == Screen::Contents) save_progress();
      if (screen_ == Screen::Light && board_.storage != nullptr) save_settings(*board_.storage, settings_);
      open_settings(false);
      return false;
    case Q::Sleep:
      close_quick_quietly();
      sleep();
      return false;
  }
  return false;
}

bool App::quick_key(Key key) {
  if (key == Key::Home) return close_quick(), false;
  QuickItem items[kQuickItems];
  const int n = quick_items(items);
  quick_sel_ = std::clamp(quick_sel_, 0, n - 1);
  if (key == Key::Power) return quick_activate(items[quick_sel_]);
  quick_sel_ = (quick_sel_ + (key == Key::Next ? 1 : -1) + n) % n;
  return true;
}

bool App::quick_gesture(const Gesture& g) {
  using K = Gesture::Kind;
  if (g.kind == K::SwipeUp) return close_quick(), false;
  if (g.kind != K::Tap && g.kind != K::LongPress) return false;
  const int item = hit_at(g.x, g.y);
  if (item < 0) return false;
  if (item == kOutsideItem) return close_quick(), false;
  if (item == kTrackItem || item == kTrackItem + 1) {
    // A tap on a track sets it to the nearest step.
    const int x0 = kSide + kRound + 16 + kKnob / 2;
    const int track_w = fb_.width() - 2 * x0;
    const int step = std::clamp((g.x - x0) * kLightSteps * 2 / track_w + 1, 0, 2 * kLightSteps) / 2;
    (item == kTrackItem ? settings_.light : settings_.warmth) = static_cast<uint8_t>(step);
    apply_light();
    return true;
  }
  QuickItem items[kQuickItems];
  const int n = quick_items(items);
  if (item >= n) return false;
  quick_sel_ = item;
  return quick_activate(items[item]);
}

void App::draw_quick() {
  const int w = fb_.width();
  const Strings& t = text();
  QuickItem items[kQuickItems];
  const int n = quick_items(items);
  const bool light = board_.frontlight != nullptr;
  const bool hint = fb_.height() >= 600;  // not on the reader turned on its side
  const int tiles_top = light ? kSliderTop + 2 * kSliderBlock + 6 : kSliderTop + 10;
  const int tile_rows = (n - (light ? 4 : 0) + kTileCols - 1) / kTileCols;
  const int bottom = tiles_top + tile_rows * (kTileH + kTileGap) + (hint ? 44 : 14) + 12;

  // The sheet: white, with a heavy rounded edge at the bottom.
  fb_.fill_round_rect({0, bottom - 40, w, 40}, 18, Color::Black);
  fb_.fill_round_rect({0, bottom - 43, w, 40}, 18, Color::White);
  fb_.fill_rect({0, 0, w, bottom - 30}, Color::White);
  fb_.fill_round_rect({w / 2 - 24, bottom - 12, 48, 5}, 2, Color::Black);  // the handle
  add_hit({0, bottom, w, fb_.height() - bottom}, kOutsideItem);

  char time[16];
  time_text(time, sizeof(time));
  ui::draw_status_bar(fb_, time, board_.power.battery_percent(), text().status_name);

  int sel = 0;
  if (light) {
    for (int k = 0; k < 2; ++k) {
      const int y = kSliderTop + k * kSliderBlock;
      const int value = k == 0 ? settings_.light : settings_.warmth;
      char label[16];
      if (k == 0 && value == 0) {
        std::snprintf(label, sizeof(label), "%s", t.off);
      } else {
        std::snprintf(label, sizeof(label), "%d%%", value * 100 / kLightSteps);
      }
      draw_text(fb_, ui::text_font(), kSide * 64, y + 24, k == 0 ? t.brightness : t.warmth);
      ui::draw_right(fb_, ui::text_font(), w - kSide, y + 24, label);
      const int row = y + 36;
      const Rect minus{kSide, row, kRound, kRound};
      const Rect plus{w - kSide - kRound, row, kRound, kRound};
      draw_sign(fb_, minus, false, draw_box(fb_, minus, kRound / 2, quick_sel_ == sel));
      add_hit(minus, sel++);
      draw_sign(fb_, plus, true, draw_box(fb_, plus, kRound / 2, quick_sel_ == sel));
      add_hit(plus, sel++);
      // The track between them, solid as far as the level, with a knob.
      const int x0 = kSide + kRound + 16 + kKnob / 2;
      const int track_w = w - 2 * x0;
      const int track_y = row + kRound / 2;
      ui::draw_progress(fb_, x0, track_y, track_w, value, kLightSteps);
      const int knob_x = x0 + value * track_w / kLightSteps;
      fb_.fill_round_rect({knob_x - kKnob / 2, track_y - kKnob / 2, kKnob, kKnob}, kKnob / 2, Color::Black);
      add_hit({x0 - kKnob, row, track_w + 2 * kKnob, kRound}, kTrackItem + k);
    }
  }

  // The big buttons, three to a row.
  const int tile_w = (w - 2 * kSide - (kTileCols - 1) * kTileGap) / kTileCols;
  for (int i = sel; i < n; ++i) {
    const int k = i - sel;
    const Rect r{kSide + (k % kTileCols) * (tile_w + kTileGap), tiles_top + (k / kTileCols) * (kTileH + kTileGap),
                 tile_w, kTileH};
    const Color fg = draw_box(fb_, r, 14, quick_sel_ == i);
    add_hit(r, i);
    const char* name = "";
    const char* state = nullptr;
    switch (items[i]) {
      case QuickItem::Light:
        name = t.light_short;
        state = settings_.light > 0 ? t.on : t.off;
        break;
      case QuickItem::Night:
        name = t.night_short;
        state = settings_.night ? t.on : t.off;
        break;
      case QuickItem::Contents:
        name = t.contents;
        break;
      case QuickItem::Library:
        name = t.library;
        break;
      case QuickItem::Home:
        name = t.home_short;
        break;
      case QuickItem::Settings:
        name = t.settings;
        break;
      case QuickItem::Sleep:
        name = t.sleep_short;
        break;
      default:
        break;
    }
    const Font& font =
        text_width_px(ui::text_font(), name) <= r.w - 16 ? ui::text_font() : ui::label_font_for(name, r.w - 16);
    const int name_w = text_width_px(font, name);
    if (state == nullptr) {
      draw_text(fb_, font, (r.x + (r.w - name_w) / 2) * 64, baseline_in(r, font), name, fg);
    } else {
      draw_text(fb_, font, (r.x + (r.w - name_w) / 2) * 64, r.y + 32, name, fg);
      const int state_w = text_width_px(ui::label_font(), state);
      draw_text(fb_, ui::label_font(), (r.x + (r.w - state_w) / 2) * 64, r.y + 58, state, fg);
    }
  }
  if (hint) {
    const int base = tiles_top + tile_rows * (kTileH + kTileGap) + 24;
    ui::draw_centered(fb_, ui::label_font_for(t.hint_quick, w - 2 * kSide), base, t.hint_quick);
  }
}

}  // namespace vellum

// Buttons and what they do: a press of Prev, Next, Power or Home, or a
// held press of Prev, Next or Home, goes through Settings › Buttons to an
// action, one set while reading and one in menus. Also here: which way up
// the screen is (Settings › Screen), pages turning by themselves, and
// screenshots, each of which a button can start.
//
// Menu actions stand for the keys they replace (Up for Prev, Select for
// Power ...), so every screen keeps its own handling of them. A button whose
// held press does nothing acts as soon as it goes down, as it always did;
// one that has a held action waits for its release or for kHoldKeyMs.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/app.h"
#include "vellum/screenshot.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

// Buttons screen geometry, logical pixels on the 480x800 portrait screen.
constexpr int kTitleBase = 104;
constexpr int kTabsTop = 128;
constexpr int kTabH = 44;
constexpr int kTabPad = 16;
constexpr int kRowsTop = 186;
constexpr int kRowH = 46;
constexpr int kResetH = 46;
constexpr int kPad = 12;       // highlights reach this far into the margins
constexpr int kTabItem = 100;  // hit items of the two tabs: 100 reading, 101 menus

// Toast geometry: a pill above the bottom edge.
constexpr int kToastH = 40;
constexpr int kToastRise = 96;

Key other_side(Key k) {
  if (k == Key::Prev) return Key::Next;
  if (k == Key::Next) return Key::Prev;
  return k;
}

}  // namespace

// ---- Presses and actions ---------------------------------------------------------

bool App::reading_controls() const {
  return screen_ == Screen::Reader && word_mode_ == WordMode::Off && typing_ == Typing::Off && !quick_open_;
}

bool App::key_down(Key key) {
  const Key k = keys_swapped() ? other_side(key) : key;
  const ButtonAction* set = reading_controls() ? settings_.buttons.reader : settings_.buttons.menu;
  const int held = button_input(k, true);
  if (set[held] == ButtonAction::None) return press(button_input(k, false));  // at once, as always
  key_waiting_ = true;
  key_down_ = key;
  key_short_ = button_input(k, false);
  key_held_ = held;
  key_down_at_ = steady_ms();
  return false;
}

bool App::key_up(Key key) {
  if (!key_waiting_ || key != key_down_) return false;
  key_waiting_ = false;
  // Held long enough though no timeout came (the board kept sending events).
  const uint32_t now = steady_ms();
  const bool held = now != 0 && now - key_down_at_ >= kHoldKeyMs;
  return press(held ? key_held_ : key_short_);
}

bool App::press(int input) {
  const ButtonAction* set = reading_controls() ? settings_.buttons.reader : settings_.buttons.menu;
  return act(set[input]);
}

bool App::act(ButtonAction a) {
  using A = ButtonAction;
  const bool reader = screen_ == Screen::Reader && reading_ >= 0;
  switch (a) {
    case A::None:
      return false;
    case A::Up:
      return handle_key(Key::Prev);
    case A::Down:
      return handle_key(Key::Next);
    case A::Select:
      return handle_key(Key::Power);
    case A::Back:
    case A::Home:
      return handle_key(Key::Home);
    case A::PageUp:
    case A::PageDown: {
      // As a swipe: lists that page by swiping page by this too.
      Gesture g;
      g.kind = a == A::PageDown ? Gesture::Kind::SwipeUp : Gesture::Kind::SwipeDown;
      g.x = g.y = -1;
      return handle_gesture(g);
    }
    case A::PrevPage:
    case A::NextPage:
      return reader && turn(a == A::NextPage ? 1 : -1);
    case A::PrevChapter:
    case A::NextChapter:
      if (reader) chapter_jump(a == A::NextChapter ? 1 : -1);
      return false;
    case A::Contents:
      if (reader) open_contents();
      return false;
    case A::Library:
      if (reader) save_progress();
      open_library();
      return false;
    case A::Bookmark:
      if (!reader) return false;
      toggle_bookmark();
      return true;
    case A::LightToggle:
      toggle_light();
      return screen_ != Screen::Reader;  // menus may show the level
    case A::LightScreen:
      if (screen_ != Screen::Light) open_light(screen_);
      return false;
    case A::AutoTurn:
      if (!reader) return false;
      start_auto_turn();
      return true;
    case A::FullRefresh:
      draw();
      queue_.submit_full();
      return false;
    case A::Screenshot:
      take_screenshot();
      return true;
    case A::Sleep:
      sleep();
      return false;
    case A::QuickPanel:
      if (quick_open_) {
        close_quick();
      } else if (quick_can_open()) {
        open_quick();
      }
      return false;
  }
  return false;
}

// ---- Which way up --------------------------------------------------------------
//
// Settings › Screen turns the board's own reading orientation by quarter
// turns: upside down for every screen, on its side for the reader only (the
// menus are laid out for the upright screen and stay so). On the X4 Pro a
// quarter turn one way puts the buttons on the left (checked on the device).

Rotation App::rotation_for(Screen s) const {
  int quarter = 0;
  switch (settings_.orientation) {
    case Orientation::Portrait:
      break;
    case Orientation::UpsideDown:
      quarter = 2;
      break;
    case Orientation::LandscapeLeft:
      quarter = s == Screen::Reader ? 1 : 0;
      break;
    case Orientation::LandscapeRight:
      quarter = s == Screen::Reader ? 3 : 0;
      break;
  }
  return static_cast<Rotation>((static_cast<int>(board_.rotation) + quarter) % 4);
}

// Prev and Next trade places when the turn would leave Next above or to the
// left of Prev (upside down, or on its side with the buttons on the right),
// so Next stays the button towards where the reading goes.
bool App::keys_swapped() const {
  const int quarter = (static_cast<int>(rotation_for(screen_)) - static_cast<int>(board_.rotation) + 4) % 4;
  return quarter == 2 || quarter == 3;
}

// ---- The buttons screen ------------------------------------------------------------
//
// Rows: the two sets as tabs (While reading, In menus), a row per input
// with its action (Power gives it the next one), Reset to default, then a
// word on what cannot be changed.

void App::open_buttons() {
  buttons_sel_ = 0;
  buttons_menus_ = false;
  buttons_refused_ = false;
  show(Screen::Buttons);
}

void App::draw_buttons() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_back_title(t.buttons, kTitleBase);

  // The set shown is the solid tab; the cursor on this row rings it.
  int x = kSide;
  for (int k = 0; k < 2; ++k) {
    const Rect r{x, kTabsTop, text_width_px(ui::text_font(), t.button_pages[k]) + 2 * kTabPad, kTabH};
    const bool current = (k == 1) == buttons_menus_;
    fb_.fill_round_rect(r, kTabH / 2, Color::Black);
    if (!current) fb_.fill_round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, kTabH / 2 - 2, Color::White);
    if (current && buttons_sel_ == 0) {
      fb_.fill_round_rect({r.x + 3, r.y + 3, r.w - 6, r.h - 6}, kTabH / 2 - 3, Color::White);
      fb_.fill_round_rect({r.x + 5, r.y + 5, r.w - 10, r.h - 10}, kTabH / 2 - 5, Color::Black);
    }
    draw_text(fb_, ui::text_font(), (r.x + kTabPad) * 64, r.y + 26, t.button_pages[k],
              current ? Color::White : Color::Black);
    add_hit(r, kTabItem + k);
    x += r.w + 10;
  }

  const ButtonAction* set = buttons_menus_ ? settings_.buttons.menu : settings_.buttons.reader;
  for (int i = 0; i < kButtonInputs; ++i) {
    const Rect r{kSide - kPad, kRowsTop + i * kRowH, w - 2 * (kSide - kPad), kRowH};
    const bool selected = buttons_sel_ == i + 1;
    if (selected) fb_.fill_round_rect(r, 10, Color::Black);
    const Color fg = selected ? Color::White : Color::Black;
    const int base = r.y + 31;
    const char* name = t.input_names[i];
    draw_text(fb_, ui::text_font(), (r.x + kPad) * 64, base, name, fg);
    const int right = r.x + r.w - kPad;
    const int room = right - (r.x + kPad + text_width_px(ui::text_font(), name) + 16);
    const char* value = t.action_names[static_cast<int>(set[i])];
    ui::draw_fitted(fb_, ui::text_font(), right - std::min(room, text_width_px(ui::text_font(), value)), base, room,
                    value, fg);
    add_hit(r, i + 1);
  }

  const int reset_y = kRowsTop + kButtonInputs * kRowH + 10;
  const Rect reset{kSide - kPad, reset_y, w - 2 * (kSide - kPad), kResetH};
  const bool on_reset = buttons_sel_ == kButtonInputs + 1;
  if (on_reset) fb_.fill_round_rect(reset, 10, Color::Black);
  draw_text(fb_, ui::text_font(), (reset.x + kPad) * 64, reset.y + 30, t.reset_default,
            on_reset ? Color::White : Color::Black);
  add_hit(reset, kButtonInputs + 1);

  // What stays as it is: Power held, and a way back.
  int y = reset_y + kResetH + 30;
  y = ui::draw_wrapped(fb_, ui::label_font(), kSide, y, w - 2 * kSide, 2, t.power_hold_note, false);
  if (buttons_refused_) fb_.fill_rect({kSide - 10, y - 4, 3, 46}, Color::Black);  // that is why nothing changed
  ui::draw_wrapped(fb_, ui::label_font(), kSide, y + 14, w - 2 * kSide, 3, t.way_back_note, false);
  ui::draw_hint(fb_, t.hint_settings);
}

bool App::buttons_activate() {
  buttons_refused_ = false;
  if (buttons_sel_ >= kTabItem) {  // a tab, tapped
    buttons_menus_ = buttons_sel_ == kTabItem + 1;
    buttons_sel_ = 0;
    return true;
  }
  if (buttons_sel_ == 0) {
    buttons_menus_ = !buttons_menus_;
    return true;
  }
  if (buttons_sel_ == kButtonInputs + 1) {
    settings_.buttons = kDefaultButtons;
  } else {
    const int input = buttons_sel_ - 1;
    ButtonAction(&set)[kButtonInputs] = buttons_menus_ ? settings_.buttons.menu : settings_.buttons.reader;
    const ButtonAction next = next_button_action(set, !buttons_menus_, input);
    buttons_refused_ = next == set[input];
    set[input] = next;
  }
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
  return true;
}

// ---- Reader actions ----------------------------------------------------------------

// By the contents entries, as listed in Contents (the book's chapters when it has none).
void App::chapter_jump(int step) {
  const Document& doc = *reader_.document();
  const int count = doc.toc_count() > 0 ? doc.toc_count() : doc.chapter_count();
  const int current = current_chapter_row();  // -1 before the first entry
  int target = current + (step > 0 ? 1 : 0);
  if (step < 0 && current >= 0 && reader_.compare_to_page(chapter_start(current)) == 0) target = current - 1;
  if (target < 0 || target >= count) return;
  jump_to(chapter_start(target));
}

void App::toggle_bookmark() {
  const int mark = page_bookmark();
  if (mark >= 0) {
    library_.remove_bookmark(mark);
  } else {
    char snippet[sizeof(Bookmark::snippet)];
    reader_.snippet(snippet, sizeof(snippet));
    library_.add_bookmark(reading_, reader_.position(), static_cast<uint8_t>(reader_.percent()), snippet);
  }
  if (board_.storage != nullptr) library_.save_bookmarks(*board_.storage);
}

void App::toggle_light() {
  if (settings_.light > 0) {
    light_before_ = settings_.light;
    settings_.light = 0;
  } else {
    settings_.light = light_before_ > 0 ? light_before_ : kLightSteps / 2;
  }
  apply_light();
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
}

// ---- Pages turning by themselves ---------------------------------------------------
//
// Every Settings › Reading › Auto page turn seconds while the reader shows.
// Each turn counts as use, so the reader does not sleep while it runs; it
// stops at the end of the book (the idle time runs again from there), on
// leaving the reader, and at any press or touch.

void App::start_auto_turn() {
  const unsigned seconds = kAutoTurnSeconds[settings_.auto_turn];
  auto_turn_ = true;
  auto_turn_at_ = steady_ms() + seconds * 1000u;
  char notice[64];
  std::snprintf(notice, sizeof(notice), text().auto_turn_on, seconds);
  show_toast(notice);
}

void App::stop_auto_turn() {
  auto_turn_ = false;
  show_toast(text().auto_turn_off);
}

void App::auto_turn_step() {
  const uint32_t interval = kAutoTurnSeconds[settings_.auto_turn] * 1000u;
  const uint32_t now = steady_ms();
  auto_turn_at_ = now != 0 ? now + interval : auto_turn_at_ + interval;
  // The last page stays: nothing more to turn to.
  if (!turn(1) || at_last_page()) auto_turn_ = false;
  draw();
  queue_.submit();
}

// ---- Screenshots and notices -------------------------------------------------------

void App::take_screenshot() {
  const Strings& t = text();
  if (toast_[0] != '\0') {
    toast_[0] = '\0';  // not in the picture
    draw();
  }
  char path[64];
  bool saved = false;
  if (board_.storage != nullptr) {
    screenshot::next_path(*board_.storage, path, sizeof(path));
    saved = screenshot::save_bmp(*board_.storage, fb_, path);
  }
  char notice[96];
  if (saved) {
    std::snprintf(notice, sizeof(notice), t.screenshot_saved, path + std::strlen(screenshot::kFolder) + 1);
  } else {
    std::snprintf(notice, sizeof(notice), "%s", t.screenshot_failed);
  }
  show_toast(notice);
}

void App::show_toast(const char* text_now) {
  std::snprintf(toast_, sizeof(toast_), "%s", text_now);
  toast_at_ = steady_ms();
}

void App::draw_toast() {
  if (toast_[0] == '\0') return;
  const int room = fb_.width() - 2 * kSide;
  const int tw = std::min(text_width_px(ui::label_font(), toast_), room - 40);
  const Rect r{(fb_.width() - tw - 40) / 2, fb_.height() - kToastRise, tw + 40, kToastH};
  fb_.fill_round_rect(r, kToastH / 2, Color::Black);
  ui::draw_fitted(fb_, ui::label_font(), r.x + 20, r.y + 26, tw, toast_, Color::White);
}

}  // namespace vellum

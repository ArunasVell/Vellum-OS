// The first-time setup: a few screens before the home screen the first time
// the reader starts with a memory card Vellum has never used, and again from
// Settings › Setup again. One step per screen: the language, the clock, the
// home Wi-Fi (the phone page of Settings › Updates, which sets the clock
// too), and how books are added (the Add books screen). Each has Skip and
// Next; leaving the phone page or Add books comes back to the next step.
// Finishing (or skipping the last step) saves Settings::setup_done.
//
// The side buttons move between the choices and the buttons, Power chooses,
// Home (or a held side button) goes back a step. A tap chooses too.
#include <cstdio>
#include <string_view>

#include "vellum/app.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

// Geometry, logical pixels on the 480x800 portrait screen.
constexpr int kTitleBase = 112;
constexpr int kStepBase = 142;
constexpr int kLeadBase = 198;
constexpr int kLeadLines = 5;
constexpr int kRowHeight = 50;
constexpr int kButtonHeight = 50;
constexpr int kButtonGap = 16;
constexpr int kHintRoom = 56;  // below the buttons
constexpr int kPad = 12;       // highlights reach this far into the margins

// The steps, in order. (A menu text size step would go after the clock once
// Settings has one.)
enum Step : int { kLanguage, kClock, kHomeWifi, kBooks, kSteps };

// What a step offers: its choices or its way in, then Skip and Next.
enum class Item : uint8_t { English, Lithuanian, Clock24, Clock12, HomeWifi, AddBooks, Skip, Next };
constexpr int kMaxItems = 4;

int step_items(int step, Item (&out)[kMaxItems]) {
  int n = 0;
  switch (step) {
    case kLanguage:
      out[n++] = Item::English;
      out[n++] = Item::Lithuanian;
      break;
    case kClock:
      out[n++] = Item::Clock24;
      out[n++] = Item::Clock12;
      break;
    case kHomeWifi:
      out[n++] = Item::HomeWifi;
      break;
    default:
      out[n++] = Item::AddBooks;
      break;
  }
  out[n++] = Item::Skip;
  out[n++] = Item::Next;
  return n;
}

Rect row_rect(const Framebuffer& fb, int y, int h) { return {kSide - kPad, y, fb.width() - 2 * (kSide - kPad), h}; }

Color highlight(Framebuffer& fb, Rect r, bool selected) {
  if (selected) fb.fill_round_rect(r, 10, Color::Black);
  return selected ? Color::White : Color::Black;
}

void dotted_rule(Framebuffer& fb, int y) {
  for (int x = kSide; x < fb.width() - kSide; x += 4) fb.set_pixel(x, y, Color::Black);
}

// A round choice centred on (cx, cy): a ring, with a dot when it is chosen.
void draw_radio(Framebuffer& fb, int cx, int cy, bool on, Color c) {
  const Color paper = c == Color::Black ? Color::White : Color::Black;
  fb.fill_round_rect({cx - 10, cy - 10, 20, 20}, 10, c);
  fb.fill_round_rect({cx - 8, cy - 8, 16, 16}, 8, paper);
  if (on) fb.fill_round_rect({cx - 5, cy - 5, 10, 10}, 5, c);
}

// An outlined, rounded button with its label centred; inverted under the cursor.
void draw_button(Framebuffer& fb, Rect r, std::string_view label, bool selected) {
  fb.fill_round_rect(r, r.h / 2, Color::Black);
  if (!selected) fb.fill_round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, r.h / 2 - 2, Color::White);
  const Color c = selected ? Color::White : Color::Black;
  const int text_w = text_width_px(ui::text_font(), label);
  if (text_w <= r.w - 24) {
    draw_text(fb, ui::text_font(), (r.x + (r.w - text_w) / 2) * 64, r.y + 32, label, c);
  } else {
    ui::draw_fitted(fb, ui::text_font(), r.x + 12, r.y + 32, r.w - 24, label, c);
  }
}

}  // namespace

void App::open_setup(int step) {
  if (step >= kSteps) return finish_setup();
  setup_step_ = step < 0 ? 0 : step;
  // The choice made so far, else the way in.
  setup_sel_ = 0;
  if (setup_step_ == kLanguage) setup_sel_ = static_cast<int>(settings_.language);
  if (setup_step_ == kClock) setup_sel_ = settings_.time_format == TimeFormat::H24 ? 0 : 1;
  show(Screen::Setup);
}

void App::finish_setup() {
  settings_.setup_done = true;
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
  show(Screen::Home);
}

void App::setup_back() {
  if (setup_step_ > 0) return open_setup(setup_step_ - 1);
  queue_.submit_full();  // the first step: just clean the screen
}

bool App::setup_key(Key key) {
  if (key == Key::Power) return setup_activate();
  Item items[kMaxItems];
  const int n = step_items(setup_step_, items);
  const int step = key == Key::Next ? 1 : key == Key::Prev ? -1 : 0;
  setup_sel_ = (setup_sel_ + step + n) % n;
  return true;
}

bool App::setup_activate() {
  Item items[kMaxItems];
  const int n = step_items(setup_step_, items);
  if (setup_sel_ < 0 || setup_sel_ >= n) return false;
  switch (items[setup_sel_]) {
    case Item::English:
    case Item::Lithuanian:
      // At once: the screen changes language with it.
      settings_.language = items[setup_sel_] == Item::English ? Language::English : Language::Lithuanian;
      break;
    case Item::Clock24:
    case Item::Clock12:
      settings_.time_format = items[setup_sel_] == Item::Clock24 ? TimeFormat::H24 : TimeFormat::H12;
      break;
    case Item::HomeWifi:
    case Item::AddBooks:
      // Leaving either screen comes back here, at the next step (close_transfer()).
      setup_transfer_ = true;
      open_transfer(items[setup_sel_] == Item::HomeWifi);
      return false;
    case Item::Skip:
    case Item::Next:
      open_setup(setup_step_ + 1);
      return false;
  }
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
  setup_sel_ = n - 1;  // chosen: Next is a press away
  draw();
  queue_.submit_full();  // a new language redraws every word
  return false;
}

void App::draw_setup() {
  const int w = fb_.width();
  const Strings& t = text();
  const char* titles[kSteps] = {t.welcome, t.clock, t.home_wifi, t.add_books};
  const char* leads[kSteps] = {t.welcome_lead, t.clock_lead, t.wifi_lead, t.books_lead};
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, titles[setup_step_]);
  char line[96];
  std::snprintf(line, sizeof(line), t.setup_step, setup_step_ + 1, static_cast<int>(kSteps));
  draw_text(fb_, ui::label_font(), kSide * 64, kStepBase, line);
  int y =
      ui::draw_wrapped(fb_, ui::text_font(), kSide, kLeadBase, w - 2 * kSide, kLeadLines, leads[setup_step_], false);

  // The home Wi-Fi already set, when it is.
  if (setup_step_ == kHomeWifi && home_wifi_set()) {
    char network[40];
    board_.updater->home_network(network, sizeof(network));
    std::snprintf(line, sizeof(line), t.wifi_saved, network);
    ui::draw_fitted(fb_, ui::label_font(), kSide, y + 2, w - 2 * kSide, line);
    y += 30;
  }

  // The choices, or the way in to the phone page or Add books.
  Item items[kMaxItems];
  const int n = step_items(setup_step_, items);
  y += 6;
  DateTime sample{};
  sample.hour = 14;
  sample.minute = 5;
  for (int i = 0; i < n - 2; ++i) {
    const Rect r = row_rect(fb_, y, kRowHeight);
    const bool selected = i == setup_sel_;
    if (i > 0 && !selected && setup_sel_ != i - 1) dotted_rule(fb_, r.y);
    add_hit(r, i);
    const Color fg = highlight(fb_, r, selected);
    const int base = r.y + 32;
    const int right = r.x + r.w - kPad;
    char name[96];
    bool radio = true;
    bool on = false;
    switch (items[i]) {
      case Item::English:
      case Item::Lithuanian: {
        const Language lang = items[i] == Item::English ? Language::English : Language::Lithuanian;
        std::snprintf(name, sizeof(name), "%s", strings(lang).language_name);
        on = settings_.language == lang;
        break;
      }
      case Item::Clock24:
      case Item::Clock12: {
        // With the time as it would look: "24-hour · 14:05".
        const TimeFormat f = items[i] == Item::Clock24 ? TimeFormat::H24 : TimeFormat::H12;
        char example[16];
        format_time(sample, f, example, sizeof(example));
        std::snprintf(name, sizeof(name), "%s \xC2\xB7 %s", f == TimeFormat::H24 ? t.clock_24 : t.clock_12, example);
        on = settings_.time_format == f;
        break;
      }
      default:
        std::snprintf(name, sizeof(name), "%s", items[i] == Item::HomeWifi ? t.set_up_now : t.open_add_books);
        radio = false;
        break;
    }
    ui::draw_fitted(fb_, ui::text_font(), r.x + kPad, base, r.w - 2 * kPad - 32, name, fg);
    if (radio) {
      draw_radio(fb_, right - 10, r.y + r.h / 2, on, fg);
    } else {
      ui::draw_right(fb_, ui::text_font(), right, base, "\xE2\x80\xBA", fg);  // ›
    }
    y += kRowHeight;
  }

  // Skip and Next (Done on the last step), side by side above the hint.
  const int button_w = (w - 2 * kSide - kButtonGap) / 2;
  const int button_y = fb_.height() - kHintRoom - kButtonHeight;
  const Rect skip{kSide, button_y, button_w, kButtonHeight};
  const Rect next{kSide + button_w + kButtonGap, button_y, button_w, kButtonHeight};
  draw_button(fb_, skip, t.skip, setup_sel_ == n - 2);
  draw_button(fb_, next, setup_step_ + 1 == kSteps ? t.done : t.next, setup_sel_ == n - 1);
  add_hit(skip, n - 2);
  add_hit(next, n - 1);

  ui::draw_hint(fb_, setup_step_ == 0 ? t.hint_welcome_first : t.hint_welcome);
}

}  // namespace vellum

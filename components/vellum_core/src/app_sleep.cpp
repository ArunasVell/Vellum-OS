// Sleep screens: what the screen keeps while the reader sleeps, chosen in
// Settings › Sleep screen. E-ink holds its last picture without power, so
// what is drawn here stays until the reader wakes, and nothing on it can
// change meanwhile (no clock).
//
// Book cover: the current book's cover fills the screen (a drawn cover
// when it has no picture). My pictures: one of the reader's own pictures
// (SleepPictures), the cover when none of them decodes. Reading stats: the
// current book's progress and time left, with today's and the week's
// reading. Minimal: the book's title and author in large type on a white
// page. Page as left: the last screen, with a small badge. An empty
// battery adds a band asking for a charge to each of them.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "vellum/app.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

constexpr int kSleepBand = 120;  // the empty-battery message along the bottom
constexpr int kPad = 12;         // selection highlights extend this far into the margins

// Settings › Sleep screen.
constexpr int kTitleBase = 104;
constexpr int kKindsTop = 124;
constexpr int kKindHeight = 50;
constexpr int kRowHeight = 48;

// The pictures screen: thumbnails in a grid of three by two.
constexpr int kGridTop = 168;
constexpr int kGridColumns = 3;
constexpr int kCellHeight = 250;

// The reading stats sleep screen.
constexpr int kDashLabelBase = 96;
constexpr int kTileHeight = 70;
constexpr int kWeekBarsHeight = 110;
constexpr int kWeekDays = 7;

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

// A ticked box on a white edge, for a marked picture.
void draw_mark(Framebuffer& fb, int x, int y) {
  constexpr int kBox = 26;
  fb.fill_round_rect({x - 3, y - 3, kBox + 6, kBox + 6}, 6, Color::White);
  fb.fill_round_rect({x, y, kBox, kBox}, 4, Color::Black);
  for (int i = 0; i < 6; ++i) fb.fill_rect({x + 6 + i, y + 12 + i, 3, 3}, Color::White);
  for (int i = 0; i < 11; ++i) fb.fill_rect({x + 11 + i, y + 17 - i, 3, 3}, Color::White);
}

}  // namespace

// ---- The sleep screen ----------------------------------------------------------

void App::draw_sleep(bool empty, bool preview) {
  hit_count_ = 0;
  // Upright, as the menus are: only the page as left keeps the reader's way up.
  if (settings_.sleep_screen != SleepScreen::Page || !drawn_) fb_.set_rotation(rotation_for(Screen::Home));
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  switch (settings_.sleep_screen) {
    case SleepScreen::Page:
      // Nothing drawn yet (it ran out of battery at start): the cover instead.
      drawn_ ? draw_sleep_badge() : draw_sleep_cover();
      break;
    case SleepScreen::Minimal:
      draw_sleep_minimal();
      break;
    case SleepScreen::Stats:
      draw_sleep_stats();
      break;
    case SleepScreen::Pictures:
      if (!draw_sleep_picture(preview)) draw_sleep_cover();
      break;
    case SleepScreen::Cover:
      draw_sleep_cover();
      break;
  }
  if (empty) {
    // An empty battery says so in a white band along the bottom.
    fb_.fill_rect({0, h - kSleepBand, w, kSleepBand}, Color::White);
    fb_.hline(0, h - kSleepBand, w, Color::Black);
    ui::draw_centered(fb_, ui::text_font(), h - kSleepBand + 44, t.battery_empty);
    ui::draw_hint(fb_, t.charge_hint);
  } else if (preview) {
    // A preview goes back with any button; the real thing wakes with Power.
    const int tw = text_width_px(ui::label_font(), t.hint_preview);
    const Rect r{(w - tw) / 2 - 14, h - ui::kHintBase - 22, tw + 28, 32};
    fb_.fill_round_rect(r, 16, Color::White);
    fb_.fill_round_rect({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, 15, Color::Black);
    ui::draw_centered(fb_, ui::label_font(), h - ui::kHintBase, t.hint_preview, Color::White);
  }
}

void App::draw_sleep_cover() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  fb_.clear(Color::White);
  if (library_.count() == 0) {
    ui::draw_centered(fb_, ui::label_font(), h / 2, t.sleeping);
    ui::draw_hint(fb_, t.wake_hint);
    return;
  }
  // The book being read fills the screen, which keeps it while asleep.
  const LibraryEntry& book = library_.entry(library_.current());
  const CoverImage* picture = cover_of(library_.current(), true);
  if (picture != nullptr) {
    ui::draw_picture(fb_, {0, 0, w, h}, *picture);  // edge to edge, cut to the screen's shape
  } else {
    ui::draw_cover(fb_, {0, 0, w, h}, book.title, book.author, nullptr);
  }
}

bool App::draw_sleep_picture(bool preview) {
  if (board_.storage == nullptr) return false;
  Storage& card = *board_.storage;
  sleep_pictures_.scan(card);
  // The shuffle varies with the time and with each sleep.
  DateTime now{};
  const uint32_t clock = board_.clock != nullptr && board_.clock->now(now) ? seconds_since_2000(now) : 0;
  const uint32_t seed = steady_ms() ^ clock ^ (sleeps_ * 2654435761u);
  const int i = sleep_pictures_.choose(card, settings_.sleep_order, seed, !preview);
  if (i < 0) return false;
  char path[64];
  sleep_pictures_.cache_path(i, path, sizeof(path));
  const CoverImage* picture = covers::load_screen(card, path);
  if (picture == nullptr) return false;
  ui::draw_picture(fb_, {0, 0, fb_.width(), fb_.height()}, *picture);
  return true;
}

void App::draw_sleep_badge() {
  // A dark pill over the middle of the footer (the page number, or a
  // hint), on a white edge so it stands out on any screen.
  const Strings& t = text();
  const Font& font = ui::label_font();
  const int tw = text_width_px(font, t.sleeping);
  const Rect r{(fb_.width() - (tw + 46)) / 2, fb_.height() - 42, tw + 46, 32};
  fb_.fill_round_rect({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 19, Color::White);
  fb_.fill_round_rect(r, 16, Color::Black);
  // A crescent moon: a white disc with a dark one over most of it.
  const int cx = r.x + 18, cy = r.y + r.h / 2;
  fb_.fill_round_rect({cx - 7, cy - 7, 14, 14}, 7, Color::White);
  fb_.fill_round_rect({cx - 3, cy - 10, 14, 14}, 7, Color::Black);
  draw_text(fb_, font, (r.x + 32) * 64, r.y + 22, t.sleeping, Color::White);
}

void App::draw_sleep_minimal() {
  const int w = fb_.width();
  const int h = fb_.height();
  constexpr int kInset = kSide + 16;
  fb_.clear(Color::White);
  if (library_.count() == 0) {
    ui::draw_centered(fb_, ui::screen_title_font(), h * 2 / 5, "Vellum");
  } else {
    // The book's title in large type, a short rule, its author.
    const LibraryEntry& book = library_.entry(library_.current());
    const int end = ui::draw_wrapped(fb_, ui::screen_title_font(), kInset, h / 3, w - 2 * kInset, 4, book.title, true);
    const int rule = end - ui::screen_title_font().line_height + 30;
    fb_.fill_rect({w / 2 - 24, rule, 48, 2}, Color::Black);
    if (book.author[0] != '\0') {
      ui::draw_wrapped(fb_, ui::italic_font(), kInset, rule + 48, w - 2 * kInset, 2, book.author, true);
    }
    draw_text(fb_, ui::small_italic_font(), kSide * 64, h - 40, "Vellum");
  }
  ui::draw_battery(fb_, w - kSide, h - 40, board_.power.battery_percent());
}

void App::draw_sleep_stats() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  const Language lang = settings_.language;
  fb_.clear(Color::White);
  int y = kDashLabelBase;

  if (library_.count() > 0) {
    // The book: its title and author, how far it is read and what is left.
    const int current = library_.current();
    const LibraryEntry& book = library_.entry(current);
    ui::draw_label(fb_, kSide, y, t.now_reading);
    y = ui::draw_wrapped(fb_, ui::screen_title_font(), kSide, y + 50, w - 2 * kSide, 2, book.title, false);
    if (book.author[0] != '\0') {
      ui::draw_fitted(fb_, ui::small_italic_font(), kSide, y - 8, w - 2 * kSide, book.author);
      y += 26;
    }
    const BookState& state = library_.state(current);
    const int percent = state.finished ? 100 : state.percent;
    char value[48];
    std::snprintf(value, sizeof(value), "%d%%", percent);
    draw_text(fb_, ui::screen_title_font(), kSide * 64, y + 40, value);
    char chapter[24], left[24];
    if (state.finished) {
      ui::draw_right(fb_, ui::text_font(), w - kSide, y + 40, t.status_finished);
    } else if (current == reading_ && time_left(chapter, left, sizeof(left))) {
      std::snprintf(value, sizeof(value), t.left_in_book, left);
      ui::draw_right(fb_, ui::text_font(), w - kSide, y + 40, value);
    }
    ui::draw_progress(fb_, kSide, y + 62, w - 2 * kSide, percent, 100);
    y += 110;
  } else {
    draw_text(fb_, ui::screen_title_font(), kSide * 64, y + 50, "Vellum");
    y += 90;
  }

  // Three figures side by side: today, the last seven days, books finished.
  const int day = today(board_.clock);
  char today_text[24] = "\xE2\x80\x93", week_text[24] = "\xE2\x80\x93", done_text[16];  // – without the clock
  if (day >= 0) {
    format_duration(lang, static_cast<int>(stats_.seconds_between(day, day) / 60), today_text, sizeof(today_text));
    format_duration(lang, static_cast<int>(stats_.seconds_between(day - (kWeekDays - 1), day) / 60), week_text,
                    sizeof(week_text));
  }
  int finished = 0;
  for (int i = 0; i < library_.count(); ++i) finished += library_.state(i).finished ? 1 : 0;
  std::snprintf(done_text, sizeof(done_text), "%d", finished);
  const char* labels[] = {t.today_label, t.last_7_days, t.finished_label};
  const char* values[] = {today_text, week_text, done_text};
  const int span = w - 2 * kSide;
  const int edges[] = {kSide, kSide + span * 33 / 100, kSide + span * 72 / 100, w - kSide};  // the count is short
  dotted_rule(fb_, y);
  for (int i = 0; i < 3; ++i) {
    const int x = edges[i] + (i > 0 ? 10 : 0);
    if (i > 0) fb_.vline(edges[i], y + 12, kTileHeight - 18, Color::Black);
    ui::draw_label(fb_, x, y + 30, labels[i]);
    // Large figures, or smaller ones when a long duration would not fit.
    const int room = edges[i + 1] - x - 4;
    const bool fits = text_width_px(ui::large_font(), values[i]) <= room;
    ui::draw_fitted(fb_, fits ? ui::large_font() : ui::text_font(), x, y + 64, room, values[i]);
  }
  y += kTileHeight + 8;
  dotted_rule(fb_, y);

  // The last seven days as small bars, today on the right.
  if (day >= 0) {
    const int first = day - (kWeekDays - 1);
    int minutes[kWeekDays];
    int top = 30;  // a short week does not fill the chart
    for (int i = 0; i < kWeekDays; ++i) {
      minutes[i] = static_cast<int>(stats_.seconds_between(first + i, first + i) / 60);
      top = std::max(top, minutes[i]);
    }
    const int slot = (w - 2 * kSide) / kWeekDays;
    const int bottom = y + 40 + kWeekBarsHeight;
    for (int i = 0; i < kWeekDays; ++i) {
      const int cx = kSide + i * slot + slot / 2;
      const int bar = std::max(2, minutes[i] * kWeekBarsHeight / top);
      fb_.fill_rect({cx - 9, bottom - bar, 18, bar}, Color::Black);
      const char* name = t.weekdays[(first + i + 5) % 7];  // 2000-01-01 was a Saturday
      const int tw = text_width_px(ui::label_font(), name);
      draw_text(fb_, ui::label_font(), (cx - tw / 2) * 64, bottom + 24, name);
    }
  }

  // Asleep, and the charge it went to sleep with.
  ui::draw_label(fb_, kSide, h - 40, t.sleeping);
  ui::draw_battery(fb_, w - kSide, h - 40, board_.power.battery_percent());
}

// ---- Settings › Sleep screen -------------------------------------------------

void App::open_sleep_setup() {
  sleep_sel_ = static_cast<int>(settings_.sleep_screen);
  if (board_.storage != nullptr) sleep_pictures_.scan(*board_.storage);
  show(Screen::SleepSetup);
}

void App::sleep_setup_activate() {
  constexpr int kKinds = 5;
  if (sleep_sel_ == kKinds + 1) return open_pictures();
  if (sleep_sel_ == kKinds + 2) return show(Screen::SleepPreview);
  if (sleep_sel_ < kKinds) {
    settings_.sleep_screen = static_cast<SleepScreen>(sleep_sel_);
  } else {
    settings_.sleep_order = settings_.sleep_order == SleepOrder::Shuffle ? SleepOrder::One : SleepOrder::Shuffle;
  }
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
  draw();
  queue_.submit();
}

void App::draw_sleep_setup() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_back_title(t.sleep_screen, kTitleBase);

  // The five kinds, the one chosen marked.
  int y = kKindsTop;
  for (int i = 0; i < 5; ++i) {
    const Rect r = row_rect(fb_, y, kKindHeight);
    add_hit(r, i);
    const Color fg = highlight(fb_, r, i == sleep_sel_);
    draw_text(fb_, ui::text_font(), (r.x + kPad) * 64, r.y + 33, t.sleep_kinds[i], fg);
    draw_radio(fb_, r.x + r.w - kPad - 10, r.y + r.h / 2, i == static_cast<int>(settings_.sleep_screen), fg);
    y += kKindHeight;
  }
  y = ui::draw_wrapped(fb_, ui::small_italic_font(), kSide, y + 32, w - 2 * kSide, 2,
                       t.sleep_help[static_cast<int>(settings_.sleep_screen)], false);

  // The reader's own pictures: in what order, and which.
  ui::draw_label(fb_, kSide, y + 28, t.pictures_label);
  y += 40;
  char count[24];
  std::snprintf(count, sizeof(count), "%d \xE2\x80\xBA", sleep_pictures_.count());  // ›
  const char* names[] = {t.sleep_order, t.choose_pictures, t.preview};
  const char* values[] = {t.sleep_order_names[static_cast<int>(settings_.sleep_order)], count, "\xE2\x80\xBA"};
  for (int k = 0; k < 3; ++k) {
    if (k == 2) {
      y += 14;
      dotted_rule(fb_, y - 7);
    }
    const int item = 5 + k;
    const Rect r = row_rect(fb_, y, kRowHeight);
    add_hit(r, item);
    const Color fg = highlight(fb_, r, item == sleep_sel_);
    draw_text(fb_, ui::text_font(), (r.x + kPad) * 64, r.y + 32, names[k], fg);
    ui::draw_right(fb_, ui::text_font(), r.x + r.w - kPad, r.y + 32, values[k], fg);
    y += kRowHeight;
  }
  ui::draw_hint(fb_, t.hint_settings);
}

// ---- The pictures ---------------------------------------------------------------

void App::open_pictures() {
  pictures_sel_ = 0;
  if (board_.storage != nullptr) {
    Storage& card = *board_.storage;
    sleep_pictures_.scan(card);
    // New pictures take a moment each to convert: say so first.
    bool waiting = false;
    for (int i = 0; i < sleep_pictures_.count() && !waiting; ++i) waiting = !sleep_pictures_.ready(card, i);
    if (waiting) {
      fb_.clear(Color::White);
      ui::draw_centered(fb_, ui::text_font(), fb_.height() / 2, text().preparing_pictures);
      queue_.submit();
      for (int i = 0; i < sleep_pictures_.count(); ++i) sleep_pictures_.prepare(card, i);
    }
  }
  show(Screen::Pictures);
}

void App::pictures_activate() {
  const int n = sleep_pictures_.count();
  if (n == 0 || pictures_sel_ >= n) return;
  if (settings_.sleep_order == SleepOrder::One) {
    // One picture: this one, and no other.
    sleep_pictures_.clear_marks();
    sleep_pictures_.set_mark(pictures_sel_, true);
  } else {
    sleep_pictures_.set_mark(pictures_sel_, !sleep_pictures_.at(pictures_sel_).marked);
  }
  if (board_.storage != nullptr) sleep_pictures_.save(*board_.storage);
}

void App::draw_pictures() {
  const int w = fb_.width();
  const Strings& t = text();
  const bool one = settings_.sleep_order == SleepOrder::One;
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.sleep_kinds[1]);
  const int n = sleep_pictures_.count();
  if (n == 0) {
    ui::draw_wrapped(fb_, ui::text_font(), kSide, kGridTop, w - 2 * kSide, 6, t.no_pictures, false);
    ui::draw_hint(fb_, t.hint_list);
    return;
  }
  ui::draw_fitted(fb_, ui::small_italic_font(), kSide, kTitleBase + 34, w - 2 * kSide,
                  one ? t.pictures_one_lead : t.pictures_shuffle_lead);

  const int cell_w = (w - 2 * kSide) / kGridColumns;
  const int first = pictures_sel_ / kPicturesPerPage * kPicturesPerPage;
  CoverImage thumb;
  thumb.px = large_cover_.px;  // free here: no screen shows a large cover underneath
  for (int k = 0; k < kPicturesPerPage && first + k < n; ++k) {
    const int i = first + k;
    const SleepPictures::Picture& p = sleep_pictures_.at(i);
    const int x = kSide + (k % kGridColumns) * cell_w;
    const int y = kGridTop + (k / kGridColumns) * kCellHeight;
    const Rect cell{x + 2, y, cell_w - 4, kCellHeight - 12};
    add_hit(cell, i);
    const Color fg = highlight(fb_, cell, i == pictures_sel_);
    const Rect r{x + (cell_w - covers::kThumbW) / 2, y + 8, covers::kThumbW, covers::kThumbH};
    char path[64];
    sleep_pictures_.cache_path(i, path, sizeof(path));
    if (board_.storage != nullptr && thumb.px != nullptr && covers::load_thumb(*board_.storage, path, thumb)) {
      fb_.fill_rect(r, Color::White);
      ui::draw_picture(fb_, r, thumb);
      fb_.draw_rect(r, Color::Black);
    } else {
      // A picture that cannot be shown keeps its place, saying so.
      fb_.fill_rect(r, Color::White);
      fb_.draw_rect(r, Color::Black);
      ui::draw_wrapped(fb_, ui::label_font(), r.x + 8, r.y + r.h / 2, r.w - 16, 3, t.picture_broken, true);
    }
    if (p.marked) draw_mark(fb_, r.x + r.w - 32, r.y + 6);
    // Its name without the extension, cut short to fit.
    std::string_view name = p.name;
    if (const size_t dot = name.rfind('.'); dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
    const int name_w = text_width_px(ui::label_font(), name);
    const int room = cell.w - 12;
    if (name_w <= room) {
      draw_text(fb_, ui::label_font(), (cell.x + (cell.w - name_w) / 2) * 64, r.y + r.h + 22, name, fg);
    } else {
      ui::draw_fitted(fb_, ui::label_font(), cell.x + 6, r.y + r.h + 22, room, name, fg);
    }
  }
  const int pages = (n + kPicturesPerPage - 1) / kPicturesPerPage;
  if (pages > 1) {
    char page[32];
    std::snprintf(page, sizeof(page), "%d / %d", first / kPicturesPerPage + 1, pages);
    ui::draw_centered(fb_, ui::label_font(), kGridTop + 2 * kCellHeight + 4, page);
  }
  ui::draw_hint(fb_, one ? t.hint_pictures_one : t.hint_pictures_shuffle);
}

}  // namespace vellum

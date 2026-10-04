// Screens and navigation: home, library, settings and the reader.
#include "vellum/app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

// Home screen geometry, logical pixels on a 480x800 portrait screen.
constexpr int kHeroLabelBase = 98;
constexpr int kHeroTop = 112;
constexpr int kHeroHeight = 252;
constexpr int kCoverW = 150;
constexpr int kCoverH = 220;
constexpr int kShelfLabelBase = 410;
constexpr int kShelfTop = 424;
constexpr int kHomeRowHeight = 72;
constexpr int kLinkHeight = 46;

// Library and settings geometry.
constexpr int kTitleBase = 112;  // large screen title
constexpr int kSubtitleBase = 142;
constexpr int kListTop = 160;
constexpr int kSettingsTitleBase = 104;
constexpr int kSettingsTop = 116;
constexpr int kSectionGap = 38;  // uppercase section label above a group of rows
constexpr int kSettingRowHeight = 40;

constexpr int kPad = 12;  // selection highlights extend this far into the margins

Rect row_rect(const Framebuffer& fb, int y, int h) { return {kSide - kPad, y, fb.width() - 2 * (kSide - kPad), h}; }

// Inverted, rounded highlight behind a selected row; returns the text colour.
Color highlight(Framebuffer& fb, Rect r, bool selected) {
  if (selected) fb.fill_round_rect(r, 10, Color::Black);
  return selected ? Color::White : Color::Black;
}

// Dotted rule between list rows, matching the reader's progress track.
void dotted_rule(Framebuffer& fb, int y) {
  for (int x = kSide; x < fb.width() - kSide; x += 4) fb.set_pixel(x, y, Color::Black);
}

void draw_link(Framebuffer& fb, Rect r, std::string_view text, bool selected) {
  const Color c = highlight(fb, r, selected);
  draw_text(fb, fonts::literata_20, (r.x + kPad) * 64, r.y + 30, text, c);
  ui::draw_right(fb, fonts::literata_20, r.x + r.w - kPad, r.y + 30, "\xE2\x80\xBA", c);  // ›
}

int wrap_index(int i, int count) { return count <= 0 ? 0 : (i % count + count) % count; }

// One line on the settings screen: a name and a value that Power cycles.
enum class Row : uint8_t { Language, Clock, TextSize, LineSpacing, WordSpacing, Margins, Alignment, Hyphenation };
constexpr Row kRows[] = {Row::Language,    Row::Clock,   Row::TextSize,  Row::LineSpacing,
                         Row::WordSpacing, Row::Margins, Row::Alignment, Row::Hyphenation};
constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);
constexpr int kGeneralRows = 2;  // rows before the reading section

int value_count(Row r) {
  switch (r) {
    case Row::TextSize:
      return 4;
    case Row::LineSpacing:
    case Row::WordSpacing:
    case Row::Margins:
      return 3;
    default:
      return 2;
  }
}

int get(const Settings& s, Row r) {
  switch (r) {
    case Row::Language:
      return static_cast<int>(s.language);
    case Row::Clock:
      return static_cast<int>(s.time_format);
    case Row::TextSize:
      return static_cast<int>(s.text_size);
    case Row::LineSpacing:
      return static_cast<int>(s.line_spacing);
    case Row::WordSpacing:
      return static_cast<int>(s.word_spacing);
    case Row::Margins:
      return static_cast<int>(s.margins);
    case Row::Alignment:
      return s.justify ? 0 : 1;
    case Row::Hyphenation:
      return s.hyphenate ? 0 : 1;
  }
  return 0;
}

void set(Settings& s, Row r, int v) {
  switch (r) {
    case Row::Language:
      s.language = static_cast<Language>(v);
      break;
    case Row::Clock:
      s.time_format = static_cast<TimeFormat>(v);
      break;
    case Row::TextSize:
      s.text_size = static_cast<TextSize>(v);
      break;
    case Row::LineSpacing:
      s.line_spacing = static_cast<Spacing>(v);
      break;
    case Row::WordSpacing:
      s.word_spacing = static_cast<Spacing>(v);
      break;
    case Row::Margins:
      s.margins = static_cast<Spacing>(v);
      break;
    case Row::Alignment:
      s.justify = v == 0;
      break;
    case Row::Hyphenation:
      s.hyphenate = v == 0;
      break;
  }
}

const char* row_name(const Strings& t, Row r) {
  switch (r) {
    case Row::Language:
      return t.language;
    case Row::Clock:
      return t.clock;
    case Row::TextSize:
      return t.text_size;
    case Row::LineSpacing:
      return t.line_spacing;
    case Row::WordSpacing:
      return t.word_spacing;
    case Row::Margins:
      return t.margins;
    case Row::Alignment:
      return t.alignment;
    case Row::Hyphenation:
      return t.hyphenation;
  }
  return "";
}

const char* value_name(const Strings& t, Row r, int v) {
  switch (r) {
    case Row::Language:
      return strings(static_cast<Language>(v)).language_name;
    case Row::Clock:
      return v == 0 ? t.clock_24 : t.clock_12;
    case Row::TextSize:
      return t.size_names[v];
    case Row::LineSpacing:
    case Row::WordSpacing:
      return t.spacing_names[v];
    case Row::Margins:
      return t.margin_names[v];
    case Row::Alignment:
      return v == 0 ? t.justified : t.left;
    case Row::Hyphenation:
      return v == 0 ? t.on : t.off;
  }
  return "";
}

}  // namespace

App::App(Board& board)
    : board_(board),
      fb_(board.framebuffer, board.display.width(), board.display.height()),
      queue_(board.display, fb_),
      library_() {
  fb_.set_rotation(board.rotation);
  if (board_.storage != nullptr) load_settings(*board_.storage, settings_);
  library_.load(builtin_books(), board_.storage, epub_);
}

void App::reload_library() {
  epub_.close();
  epub_path_[0] = '\0';
  reading_ = -1;
  library_.load(builtin_books(), board_.storage, epub_);
  show(Screen::Home);
}

void App::save_progress() {
  turns_since_save_ = 0;
  if (board_.storage != nullptr) library_.save_progress(*board_.storage);
}

void App::time_text(char* out, size_t size) const {
  DateTime t;
  if (board_.clock != nullptr && board_.clock->now(t)) {
    format_time(t, settings_.time_format, out, size);
  } else if (size > 0) {
    out[0] = '\0';
  }
}

// ---- Home ------------------------------------------------------------------

int App::home_item_count() const {
  return 1 + std::min(kHomeRows, library_.count() - 1) + 2;  // current book, others, two links
}

void App::draw_home() {
  const int w = fb_.width();
  const int current = library_.current();
  const LibraryEntry& book = library_.entry(current);
  const BookState& state = book.state;
  const Strings& t = text();

  ui::draw_label(fb_, kSide, kHeroLabelBase, state.started() ? t.continue_reading : t.start_reading);

  // The current book: cover, title, author, chapter and progress.
  const Rect hero = row_rect(fb_, kHeroTop, kHeroHeight);
  add_hit(hero, 0);
  if (home_sel_ == 0) {
    fb_.fill_round_rect(hero, 12, Color::Black);
    fb_.fill_round_rect({hero.x + 3, hero.y + 3, hero.w - 6, hero.h - 6}, 9, Color::White);
  }
  const Rect cover{kSide, kHeroTop + (kHeroHeight - kCoverH) / 2, kCoverW, kCoverH};
  ui::draw_cover(fb_, cover, book.title, book.author);
  const int col_x = cover.x + cover.w + 22;
  const int col_w = w - kSide - col_x;
  int y = ui::draw_wrapped(fb_, fonts::literata_italic_26, col_x, cover.y + 28, col_w, 3, book.title, false);
  ui::draw_fitted(fb_, fonts::literata_20, col_x, y + 4, col_w, book.author);
  if (book.builtin != nullptr) {
    char chapter[96];
    const Book& b = *book.builtin;
    if (b.chapter_title[0] != '\0') {
      std::snprintf(chapter, sizeof(chapter), "%s \xC2\xB7 %s", b.chapter_label, b.chapter_title);  // ·
    } else {
      std::snprintf(chapter, sizeof(chapter), "%s", b.chapter_label);
    }
    ui::draw_wrapped(fb_, fonts::literata_16, col_x, y + 32, col_w, 2, chapter, false);
  }

  const int track_y = cover.y + cover.h - 6;
  if (state.started()) {
    const int percent = state.percent;
    char label[16];
    std::snprintf(label, sizeof(label), "%d%%", percent);
    ui::draw_right(fb_, fonts::literata_16, col_x + col_w, track_y - 14, label);
    ui::draw_progress(fb_, col_x, track_y, col_w, percent, 100);
  } else {
    draw_text(fb_, fonts::literata_16, col_x * 64, track_y - 14, t.not_started);
    ui::draw_progress(fb_, col_x, track_y, col_w, 0, 1);
  }

  // The rest of the shelf, most recently read first.
  ui::draw_label(fb_, kSide, kShelfLabelBase, t.library_label);
  char count[32];
  format_book_count(settings_.language, library_.count(), count, sizeof(count));
  ui::draw_right(fb_, fonts::literata_16, w - kSide, kShelfLabelBase, count);

  int order[Library::kMaxBooks];
  const int n = library_.by_recent(order, Library::kMaxBooks);
  int item = 1;
  y = kShelfTop;
  for (int k = 0; k < n && item <= kHomeRows; ++k) {
    if (order[k] == current) continue;
    const bool selected = home_sel_ == item;
    if (item > 1 && !selected && home_sel_ != item - 1) dotted_rule(fb_, y);
    ui::draw_book_row(fb_, row_rect(fb_, y, kHomeRowHeight), library_.entry(order[k]), t, selected);
    add_hit(row_rect(fb_, y, kHomeRowHeight), item);
    y += kHomeRowHeight;
    ++item;
  }
  y += 4;
  draw_link(fb_, row_rect(fb_, y, kLinkHeight), t.all_books, home_sel_ == item);
  draw_link(fb_, row_rect(fb_, y + kLinkHeight, kLinkHeight), t.settings, home_sel_ == item + 1);
  add_hit(row_rect(fb_, y, kLinkHeight), item);
  add_hit(row_rect(fb_, y + kLinkHeight, kLinkHeight), item + 1);

  ui::draw_hint(fb_, t.hint_home);
}

void App::home_activate() {
  const int count = home_item_count();
  if (home_sel_ == count - 1) return show(Screen::Settings);
  if (home_sel_ == count - 2) return show(Screen::Library);
  const int current = library_.current();
  if (home_sel_ == 0) return open_book(current);
  int order[Library::kMaxBooks];
  const int n = library_.by_recent(order, Library::kMaxBooks);
  int item = 1;
  for (int k = 0; k < n; ++k) {
    if (order[k] == current) continue;
    if (item++ == home_sel_) return open_book(order[k]);
  }
}

// ---- Library ---------------------------------------------------------------

void App::draw_library() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, fonts::literata_italic_40, kSide * 64, kTitleBase, t.library);
  char count[32];
  format_book_count(settings_.language, library_.count(), count, sizeof(count));
  draw_text(fb_, fonts::literata_16, kSide * 64, kSubtitleBase, count);

  const int pages = (library_.count() + kLibraryRows - 1) / kLibraryRows;
  const int page = library_sel_ / kLibraryRows;
  if (pages > 1) {
    char label[48];
    std::snprintf(label, sizeof(label), t.page_of, page + 1, pages);
    ui::draw_right(fb_, fonts::literata_16, w - kSide, kSubtitleBase, label);
  }

  const int first = page * kLibraryRows;
  const int last = std::min(library_.count(), first + kLibraryRows);
  for (int i = first; i < last; ++i) {
    const int y = kListTop + (i - first) * ui::kRowHeight;
    const bool selected = i == library_sel_;
    if (i > first && !selected && library_sel_ != i - 1) dotted_rule(fb_, y);
    ui::draw_book_row(fb_, row_rect(fb_, y, ui::kRowHeight), library_.entry(i), t, selected);
    add_hit(row_rect(fb_, y, ui::kRowHeight), i);
  }
  ui::draw_hint(fb_, t.hint_list);
}

void App::library_activate() { open_book(library_sel_); }

// ---- Settings --------------------------------------------------------------

void App::draw_settings() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, fonts::literata_italic_40, kSide * 64, kSettingsTitleBase, t.settings);

  int y = kSettingsTop;
  for (int i = 0; i < kRowCount; ++i) {
    if (i == 0 || i == kGeneralRows) {
      ui::draw_label(fb_, kSide, y + 26, i == 0 ? t.general_label : t.reading_label);
      y += kSectionGap;
    }
    const Row row = kRows[i];
    const bool selected = i == settings_sel_;
    const Rect r = row_rect(fb_, y, kSettingRowHeight);
    add_hit(r, i);
    const Color fg = highlight(fb_, r, selected);
    const int base = r.y + 28;
    draw_text(fb_, fonts::literata_20, (r.x + kPad) * 64, base, row_name(t, row), fg);
    ui::draw_right(fb_, fonts::literata_20, r.x + r.w - kPad, base, value_name(t, row, get(settings_, row)), fg);
    y += kSettingRowHeight;
  }

  // A few lines set exactly as the reader would set them.
  ui::draw_label(fb_, kSide, y + 30, t.preview_label);
  const int top = y + 44;
  const int bottom = fb_.height() - 52;
  dotted_rule(fb_, top);
  dotted_rule(fb_, bottom);
  const char* preview[] = {t.preview_text};
  const ReadingStyle style = reading_style(settings_);
  PageStyle page = page_style(style, fb_, t.preview_language, false);
  page.area = {style.margin, top + 10, w - 2 * style.margin, bottom - top - 14};
  typeset_page(page, preview, 1, {}, &fb_);

  ui::draw_hint(fb_, t.hint_settings);
}

void App::settings_activate() {
  const Row row = kRows[settings_sel_];
  set(settings_, row, (get(settings_, row) + 1) % value_count(row));
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
}

// ---- Navigation --------------------------------------------------------------

void App::open_book(int index) {
  const LibraryEntry& entry = library_.entry(index);
  Document* doc = &builtin_;
  if (entry.builtin != nullptr) {
    builtin_.set(*entry.builtin);
  } else {
    if (board_.storage == nullptr) return;
    if (!epub_.is_open() || std::strcmp(epub_path_, entry.path) != 0) {
      epub_path_[0] = '\0';
      if (!epub_.open(*board_.storage, entry.path)) return;  // unreadable: stay where we are
      std::snprintf(epub_path_, sizeof(epub_path_), "%s", entry.path);
    }
    doc = &epub_;
  }
  BookState& state = library_.state(index);
  if (!reader_.open(*doc, static_cast<uint32_t>(index) + 1, reading_style(settings_), state.position, fb_)) return;
  library_.touch(index);
  reading_ = index;
  state.position = reader_.position();
  state.percent = static_cast<uint8_t>(reader_.percent());
  save_progress();
  show(Screen::Reader);
}

void App::show(Screen s) {
  screen_ = s;
  if (s == Screen::Home) home_sel_ = 0;
  draw();
  queue_.submit_full();  // a new screen: clear the previous one's ghosting
}

void App::add_hit(Rect r, int item) {
  if (hit_count_ < kMaxHits) hits_[hit_count_++] = {r, item};
}

int App::hit_at(int x, int y) const {
  for (int i = 0; i < hit_count_; ++i) {
    const Rect& r = hits_[i].r;
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return hits_[i].item;
  }
  return -1;
}

void App::draw() {
  hit_count_ = 0;
  char time[16];
  time_text(time, sizeof(time));
  const int battery = board_.power.battery_percent();
  if (screen_ == Screen::Reader) {
    reader_.draw(fb_, text(), time, battery);
    return;
  }
  fb_.clear(Color::White);
  ui::draw_status_bar(fb_, time, battery);
  switch (screen_) {
    case Screen::Home:
      draw_home();
      break;
    case Screen::Library:
      draw_library();
      break;
    case Screen::Settings:
      draw_settings();
      break;
    case Screen::Reader:
      break;
  }
}

void App::start() {
  draw();
  queue_.submit_full();
}

bool App::handle(const InputEvent& ev) {
  switch (ev.type) {
    case InputEvent::Type::Quit:
      return false;
    case InputEvent::Type::KeyDown:
      if (!handle_key(ev.key)) return true;
      break;
    case InputEvent::Type::TouchDown:
    case InputEvent::Type::TouchMove:
    case InputEvent::Type::TouchUp:
      if (!handle_gesture(touch_.feed(ev, fb_))) return true;
      break;
    default:
      return true;
  }
  draw();
  queue_.submit();
  return true;
}

bool App::turn(int step) {
  if (step == 0 || !(step > 0 ? reader_.next() : reader_.prev())) return false;
  BookState& state = library_.state(reading_);
  state.position = reader_.position();
  state.percent = static_cast<uint8_t>(reader_.percent());
  // Save now and then, not on every turn: card writes cost power.
  if (++turns_since_save_ >= 10) save_progress();
  return true;
}

// Returns true when the screen needs a redraw; screen changes draw themselves.
bool App::handle_key(Key key) {
  if (key == Key::Home) {
    if (screen_ == Screen::Home) {
      queue_.submit_full();  // already home: just clean the screen
    } else {
      if (screen_ == Screen::Reader) save_progress();
      show(Screen::Home);
    }
    return false;
  }

  const int step = key == Key::Next ? 1 : key == Key::Prev ? -1 : 0;
  switch (screen_) {
    case Screen::Reader:
      return turn(step);
    case Screen::Home:
      if (key == Key::Power) return home_activate(), false;
      home_sel_ = wrap_index(home_sel_ + step, home_item_count());
      return true;
    case Screen::Library:
      if (key == Key::Power) return library_activate(), false;
      library_sel_ = wrap_index(library_sel_ + step, library_.count());
      return true;
    case Screen::Settings:
      if (key == Key::Power) settings_activate();
      settings_sel_ = wrap_index(settings_sel_ + step, kRowCount);
      return true;
  }
  return false;
}

bool App::handle_gesture(const Gesture& g) {
  using K = Gesture::Kind;
  if (g.kind == K::None) return false;

  if (screen_ == Screen::Reader) {
    switch (g.kind) {
      case K::Tap:
        return turn(g.x < fb_.width() / 3 ? -1 : 1);
      case K::SwipeLeft:
      case K::SwipeUp:
        return turn(1);
      case K::SwipeRight:
      case K::SwipeDown:
        return turn(-1);
      case K::None:
        break;
    }
    return false;
  }

  if (screen_ == Screen::Library && g.kind != K::Tap) {
    // Swipes page through the list, keeping the highlight on the same row.
    const int pages = (library_.count() + kLibraryRows - 1) / kLibraryRows;
    if (pages <= 1) return false;
    const int forward = g.kind == K::SwipeLeft || g.kind == K::SwipeUp ? 1 : -1;
    const int page = wrap_index(library_sel_ / kLibraryRows + forward, pages);
    library_sel_ = std::min(page * kLibraryRows + library_sel_ % kLibraryRows, library_.count() - 1);
    return true;
  }
  if (g.kind != K::Tap) return false;

  const int item = hit_at(g.x, g.y);
  if (item < 0) return false;
  switch (screen_) {
    case Screen::Home:
      home_sel_ = item;
      home_activate();
      return false;
    case Screen::Library:
      library_sel_ = item;
      library_activate();
      return false;
    case Screen::Settings:
      settings_sel_ = item;
      settings_activate();
      return true;
    case Screen::Reader:
      break;
  }
  return false;
}

void run(Board& board) {
  static App app(board);  // too large for a small task stack
  app.start();
  for (;;) {
    InputEvent ev;
    if (!board.input.wait(ev, 60 * 1000)) continue;
    if (!app.handle(ev)) return;
  }
}

}  // namespace vellum

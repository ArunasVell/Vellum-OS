// Screens and navigation: home, library, settings and the reader.
#include "vellum/app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/text.h"
#include "vellum/textfile.h"
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
constexpr int kSettingRowHeight = 36;

// Sleep screen geometry.
constexpr int kSleepCoverTop = 120;
constexpr int kSleepCoverSide = 90;

constexpr uint32_t kHoldMs = 1000;  // holding Power this long sleeps
// Battery levels, unless charging: at kEmptyPercent the reader sleeps with a
// "charge it" screen before the voltage sags too low for a refresh; under
// kNightUpdatePercent the night's update check is skipped.
constexpr int kEmptyPercent = 2;
constexpr int kNightUpdatePercent = 20;
constexpr const char* kStatePath = "vellum/state.txt";

// Contents geometry.
constexpr int kContentsRowHeight = 50;
constexpr int kContentsBottom = 60;  // room for the hint
constexpr int kIndent = 20;          // per level of a nested contents entry

// Battery screen geometry.
constexpr int kChartLabelBase = 196;
constexpr int kChartTop = 222;
constexpr int kChartBottom = 438;
constexpr int kChartAxis = 40;  // room left of the chart for 0, 50, 100
constexpr int kEstimatesBase = 512;
constexpr int kChartDays = 7;

// Reading stats geometry.
constexpr int kTodayLabelBase = 172;
constexpr int kTodayBase = 222;
constexpr int kWeekLabelBase = 268;
constexpr int kBarsTop = 296;  // room above the tallest bar for its figure
constexpr int kBarsBottom = 400;
constexpr int kBarW = 30;
constexpr int kStatRowsTop = 438;
constexpr int kStatsBookHeight = 52;
constexpr int kMinScaleMinutes = 30;  // a short day does not fill the chart

// Contents: the time left sits under the book's title, the rows below it.
constexpr int kContentsLeftBase = 168;
constexpr int kContentsTop = 186;

// Front light geometry.
constexpr int kLightTop = 156;    // first slider block
constexpr int kLightBlock = 130;  // one slider: name, value, track and end labels
constexpr int kLightTrack = 72;   // track, below the block's top
constexpr int kKnob = 16;
// Device check geometry.
constexpr int kKeysLabelBase = 152;
constexpr int kKeysTop = 166;
constexpr int kKeyHeight = 56;
constexpr int kTouchLabelBase = 258;
constexpr int kTouchTop = 272;
constexpr int kTouchHeight = 106;
constexpr int kAboutLabelBase = 420;
constexpr int kAboutRowHeight = 34;
constexpr int kCorner = 14;  // marks in the screen's corners

// Updates geometry.
constexpr int kUpdateStatusLabelBase = 196;
constexpr int kUpdateRowsTop = 300;
constexpr int kUpdateRowHeight = 50;
constexpr int kUpdateRows = 4;  // check or install, automatic, home Wi-Fi, from a file

// "Add books" geometry.
constexpr int kTransferTop = 192;  // baseline of the first step label
constexpr int kQrSize = 156;

constexpr int kUpdateNoticeItem = 1000;  // hit item of the home screen's update notice

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
  draw_text(fb, ui::text_font(), (r.x + kPad) * 64, r.y + 30, text, c);
  ui::draw_right(fb, ui::text_font(), r.x + r.w - kPad, r.y + 30, "\xE2\x80\xBA", c);  // ›
}

// A 2 px line; dotted lines leave every other pair of pixels out.
void draw_line(Framebuffer& fb, int x0, int y0, int x1, int y1, bool dotted, Color c = Color::Black) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (int step = 0;; ++step) {
    if (!dotted || step % 4 < 2) {
      fb.set_pixel(x0, y0, c);
      fb.set_pixel(x0, y0 + 1, c);
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

int wrap_index(int i, int count) { return count <= 0 ? 0 : (i % count + count) % count; }

// One line on the settings screen: a name and a value that Power cycles.
enum class Row : uint8_t {
  Language,
  MenuFont,     // cycles through the typefaces
  BookFont,     // the same, for reading
  ReadingPage,  // opens the reading settings
  Clock,
  SleepAfter,
  Light,    // opens the front light screen
  Battery,  // opens the battery screen
  Updates,  // opens the updates screen
  Check,    // opens the device check
  Stats,    // opens the reading stats
  TextSize,
  LineSpacing,
  WordSpacing,
  Margins,
  Alignment,
  Hyphenation,
  Footer
};
// Settings has two pages: general, ending in a way to the reading page, and
// reading, with a preview of the text below its rows.
constexpr Row kGeneralRows[] = {Row::Language, Row::MenuFont, Row::Clock, Row::SleepAfter, Row::Light,
                                Row::Battery,  Row::Updates,  Row::Stats, Row::Check,      Row::ReadingPage};
constexpr Row kReadingRows[] = {Row::BookFont, Row::TextSize,  Row::LineSpacing, Row::WordSpacing,
                                Row::Margins,  Row::Alignment, Row::Hyphenation, Row::Footer};
constexpr int kGeneralCount = sizeof(kGeneralRows) / sizeof(kGeneralRows[0]);
constexpr int kReadingCount = sizeof(kReadingRows) / sizeof(kReadingRows[0]);

int value_count(Row r) {
  switch (r) {
    case Row::TextSize:
    case Row::SleepAfter:
      return 4;
    case Row::LineSpacing:
    case Row::WordSpacing:
    case Row::Margins:
    case Row::Footer:
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
    case Row::SleepAfter:
      return static_cast<int>(s.sleep_after);
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
    case Row::Footer:
      return static_cast<int>(s.footer);
    case Row::MenuFont:
    case Row::BookFont:
    case Row::ReadingPage:
    case Row::Light:
    case Row::Battery:
    case Row::Updates:
    case Row::Check:
    case Row::Stats:
      break;
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
    case Row::SleepAfter:
      s.sleep_after = static_cast<SleepAfter>(v);
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
    case Row::Footer:
      s.footer = static_cast<FooterInfo>(v);
      break;
    case Row::MenuFont:
    case Row::BookFont:
    case Row::ReadingPage:
    case Row::Light:
    case Row::Battery:
    case Row::Updates:
    case Row::Check:
    case Row::Stats:
      break;
  }
}

const char* row_name(const Strings& t, Row r) {
  switch (r) {
    case Row::Language:
      return t.language;
    case Row::Clock:
      return t.clock;
    case Row::SleepAfter:
      return t.sleep_after;
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
    case Row::Light:
      return t.front_light;
    case Row::Battery:
      return t.battery;
    case Row::Updates:
      return t.updates;
    case Row::Check:
      return t.device_check;
    case Row::Stats:
      return t.reading_stats;
    case Row::Footer:
      return t.footer;
    case Row::MenuFont:
      return t.menu_font;
    case Row::BookFont:
      return t.book_font;
    case Row::ReadingPage:
      return t.reading;
  }
  return "";
}

const char* value_name(const Strings& t, Row r, int v) {
  switch (r) {
    case Row::Language:
      return strings(static_cast<Language>(v)).language_name;
    case Row::Clock:
      return v == 0 ? t.clock_24 : t.clock_12;
    case Row::SleepAfter:
      return t.sleep_names[v];
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
    case Row::Footer:
      return t.footer_names[v];
    case Row::MenuFont:
    case Row::BookFont:
    case Row::ReadingPage:
    case Row::Light:
    case Row::Battery:
    case Row::Updates:
    case Row::Check:
    case Row::Stats:
      break;
  }
  return "";
}

}  // namespace

App::App(Board& board)
    : board_(board),
      fb_(board.framebuffer, board.display.width(), board.display.height()),
      queue_(board.display, fb_),
      library_(),
      definition_(new(std::nothrow) char[kDefinitionBytes]),
      safe_fonts_(board.power.after_crash()) {
  large_cover_.px = new (std::nothrow) uint8_t[covers::kLargeW * covers::kLargeH];
  small_cover_.px = new (std::nothrow) uint8_t[covers::kSmallW * covers::kSmallH];
  fb_.set_rotation(board.rotation);
  if (board_.storage != nullptr) {
    load_settings(*board_.storage, settings_);
    char state[16];
    const int32_t n = textfile::read(*board_.storage, kStatePath, state, sizeof(state) - 1);
    // After a crash, home rather than the book: it may be what crashed.
    resume_reader_ = n >= 6 && std::strncmp(state, "reader", 6) == 0 && !safe_fonts_;
    battery_.load(*board_.storage);
    stats_.load(*board_.storage);
    load_update_state(*board_.storage, update_state_);
    highlights_.load(*board_.storage);
  }
  library_.load(builtin_books(), board_.storage, epub_);
  fonts_.scan(board_.storage);
  apply_fonts();
}

App::~App() {
  delete[] large_cover_.px;
  delete[] small_cover_.px;
  delete[] definition_;
}

const CoverImage* App::cover_of(int book, bool large) {
  const LibraryEntry& e = library_.entry(book);
  CoverImage& img = large ? large_cover_ : small_cover_;
  if (e.cover <= 0 || board_.storage == nullptr || img.px == nullptr) return nullptr;
  char path[64];
  covers::path_for(e.path, e.file_size, path, sizeof(path));
  return covers::load(*board_.storage, path, large, img) ? &img : nullptr;
}

void App::reload_library() {
  epub_.close();
  epub_path_[0] = '\0';
  reading_ = -1;
  library_.load(builtin_books(), board_.storage, epub_);
  fonts_.scan(board_.storage);  // fonts may have arrived with the books
  apply_fonts();
  dictionaries_scanned_ = false;  // and dictionaries
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
  return 1 + std::min(kHomeRows, library_.count() - 1) + 3;  // current book, others, three links
}

void App::draw_home() {
  const int w = fb_.width();
  const int current = library_.current();
  const LibraryEntry& book = library_.entry(current);
  const BookState& state = book.state;
  const Strings& t = text();

  ui::draw_label(fb_, kSide, kHeroLabelBase, state.started() ? t.continue_reading : t.start_reading);
  if (update_available()) {
    // A small notice: a new version is ready. Tapping it opens Updates.
    char notice[64];
    std::snprintf(notice, sizeof(notice), t.update_notice, update_state_.latest);
    const int tw = text_width(ui::label_font(), notice) / 64;
    const Rect pill{w - kSide - tw - 24, kHeroLabelBase - 22, tw + 24, 30};
    fb_.fill_round_rect(pill, 15, Color::Black);
    draw_text(fb_, ui::label_font(), (pill.x + 12) * 64, kHeroLabelBase - 1, notice, Color::White);
    add_hit({pill.x - 8, pill.y - 8, pill.w + 16, pill.h + 16}, kUpdateNoticeItem);
  }

  // The current book: cover, title, author, chapter and progress.
  const Rect hero = row_rect(fb_, kHeroTop, kHeroHeight);
  add_hit(hero, 0);
  if (home_sel_ == 0) {
    fb_.fill_round_rect(hero, 12, Color::Black);
    fb_.fill_round_rect({hero.x + 3, hero.y + 3, hero.w - 6, hero.h - 6}, 9, Color::White);
  }
  const Rect cover{kSide, kHeroTop + (kHeroHeight - kCoverH) / 2, kCoverW, kCoverH};
  ui::draw_cover(fb_, cover, book.title, book.author, cover_of(current, true));
  const int col_x = cover.x + cover.w + 22;
  const int col_w = w - kSide - col_x;
  int y = ui::draw_wrapped(fb_, ui::italic_font(), col_x, cover.y + 28, col_w, 3, book.title, false);
  ui::draw_fitted(fb_, ui::text_font(), col_x, y + 4, col_w, book.author);
  if (book.builtin != nullptr) {
    char chapter[96];
    const Book& b = *book.builtin;
    if (b.chapter_title[0] != '\0') {
      std::snprintf(chapter, sizeof(chapter), "%s \xC2\xB7 %s", b.chapter_label, b.chapter_title);  // ·
    } else {
      std::snprintf(chapter, sizeof(chapter), "%s", b.chapter_label);
    }
    ui::draw_wrapped(fb_, ui::label_font(), col_x, y + 32, col_w, 2, chapter, false);
  }

  const int track_y = cover.y + cover.h - 6;
  if (state.started()) {
    const int percent = state.percent;
    char label[16];
    std::snprintf(label, sizeof(label), "%d%%", percent);
    ui::draw_right(fb_, ui::label_font(), col_x + col_w, track_y - 14, label);
    ui::draw_progress(fb_, col_x, track_y, col_w, percent, 100);
  } else {
    draw_text(fb_, ui::label_font(), col_x * 64, track_y - 14, t.not_started);
    ui::draw_progress(fb_, col_x, track_y, col_w, 0, 1);
  }

  // The rest of the shelf, most recently read first.
  ui::draw_label(fb_, kSide, kShelfLabelBase, t.library_label);
  char count[32];
  format_book_count(settings_.language, library_.count(), count, sizeof(count));
  ui::draw_right(fb_, ui::label_font(), w - kSide, kShelfLabelBase, count);

  int order[Library::kMaxBooks];
  const int n = library_.by_recent(order, Library::kMaxBooks);
  int item = 1;
  y = kShelfTop;
  for (int k = 0; k < n && item <= kHomeRows; ++k) {
    if (order[k] == current) continue;
    const bool selected = home_sel_ == item;
    if (item > 1 && !selected && home_sel_ != item - 1) dotted_rule(fb_, y);
    ui::draw_book_row(fb_, row_rect(fb_, y, kHomeRowHeight), library_.entry(order[k]), t, selected,
                      cover_of(order[k], false));
    add_hit(row_rect(fb_, y, kHomeRowHeight), item);
    y += kHomeRowHeight;
    ++item;
  }
  y += 4;
  const char* links[] = {t.all_books, t.add_books, t.settings};
  for (int i = 0; i < 3; ++i) {
    const Rect r = row_rect(fb_, y + i * kLinkHeight, kLinkHeight);
    draw_link(fb_, r, links[i], home_sel_ == item + i);
    add_hit(r, item + i);
  }

  ui::draw_hint(fb_, t.hint_home);
}

void App::home_activate() {
  const int count = home_item_count();
  if (home_sel_ == count - 1) return open_settings(false);
  if (home_sel_ == count - 2) return open_transfer();
  if (home_sel_ == count - 3) return show(Screen::Library);
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
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.library);
  char count[32];
  format_book_count(settings_.language, library_.count(), count, sizeof(count));
  draw_text(fb_, ui::label_font(), kSide * 64, kSubtitleBase, count);

  const int pages = (library_.count() + kLibraryRows - 1) / kLibraryRows;
  const int page = library_sel_ / kLibraryRows;
  if (pages > 1) {
    char label[48];
    std::snprintf(label, sizeof(label), t.page_of, page + 1, pages);
    ui::draw_right(fb_, ui::label_font(), w - kSide, kSubtitleBase, label);
  }

  const int first = page * kLibraryRows;
  const int last = std::min(library_.count(), first + kLibraryRows);
  for (int i = first; i < last; ++i) {
    const int y = kListTop + (i - first) * ui::kRowHeight;
    const bool selected = i == library_sel_;
    if (i > first && !selected && library_sel_ != i - 1) dotted_rule(fb_, y);
    ui::draw_book_row(fb_, row_rect(fb_, y, ui::kRowHeight), library_.entry(i), t, selected, cover_of(i, false));
    add_hit(row_rect(fb_, y, ui::kRowHeight), i);
  }
  ui::draw_hint(fb_, t.hint_list);
}

void App::library_activate() { open_book(library_sel_); }

// ---- Settings --------------------------------------------------------------

void App::draw_settings() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kSettingsTitleBase, settings_reading_ ? t.reading : t.settings);

  const Row* rows = settings_reading_ ? kReadingRows : kGeneralRows;
  const int count = settings_reading_ ? kReadingCount : kGeneralCount;
  int y = kSettingsTop + 8;
  for (int i = 0; i < count; ++i) {
    const Row row = rows[i];
    const bool selected = i == settings_sel_;
    const Rect r = row_rect(fb_, y, kSettingRowHeight);
    add_hit(r, i);
    const Color fg = highlight(fb_, r, selected);
    const int base = r.y + 27;
    const char* name = row_name(t, row);
    draw_text(fb_, ui::text_font(), (r.x + kPad) * 64, base, name, fg);
    char value[64];
    if (row == Row::Light) {
      if (settings_.light == 0) {
        std::snprintf(value, sizeof(value), "%s \xE2\x80\xBA", t.off);  // ›
      } else {
        std::snprintf(value, sizeof(value), "%d%% \xE2\x80\xBA", settings_.light * 100 / kLightSteps);
      }
    } else if (row == Row::Battery) {
      // The level now, and a way in to the history.
      std::snprintf(value, sizeof(value), "\xE2\x80\x93 \xE2\x80\xBA");  // – ›
      if (const int percent = board_.power.battery_percent(); percent >= 0) {
        std::snprintf(value, sizeof(value), "%d%% \xE2\x80\xBA", percent);
      }
    } else if (row == Row::Updates) {
      std::snprintf(value, sizeof(value), "%s \xE2\x80\xBA", update_available() ? t.update_new : board_.version);
    } else if (row == Row::Stats) {
      // Today's reading so far, and a way in to the rest.
      std::snprintf(value, sizeof(value), "\xE2\x80\xBA");  // ›
      if (const int day = today(board_.clock); day >= 0 && stats_.seconds_between(day, day) >= 60) {
        format_duration(settings_.language, static_cast<int>(stats_.seconds_between(day, day) / 60), value,
                        sizeof(value) - 4);
        std::strcat(value, " \xE2\x80\xBA");
      }
    } else if (row == Row::Check || row == Row::ReadingPage) {
      std::snprintf(value, sizeof(value), "\xE2\x80\xBA");  // ›
    } else if (row == Row::MenuFont || row == Row::BookFont) {
      std::snprintf(value, sizeof(value), "%s", row == Row::MenuFont ? settings_.menu_font : settings_.book_font);
    } else {
      std::snprintf(value, sizeof(value), "%s", value_name(t, row, get(settings_, row)));
    }
    // A long font name gives way to the row's name.
    const int right = r.x + r.w - kPad;
    const int room = right - (r.x + kPad + text_width_px(ui::text_font(), name) + 16);
    if (text_width_px(ui::text_font(), value) <= room) {
      ui::draw_right(fb_, ui::text_font(), right, base, value, fg);
    } else if (room > 0) {
      ui::draw_fitted(fb_, ui::text_font(), right - room, base, room, value, fg);
    }
    y += kSettingRowHeight;
  }

  if (settings_reading_) {
    // A few lines set exactly as the reader would set them.
    ui::draw_label(fb_, kSide, y + 34, t.preview_label);
    const int top = y + 48;
    const int bottom = fb_.height() - 52;
    dotted_rule(fb_, top);
    dotted_rule(fb_, bottom);
    const char* preview[] = {t.preview_text};
    const ReadingStyle style = reading_style(settings_, &book_fonts());
    PageStyle page = page_style(style, fb_, t.preview_language, false);
    page.area = {style.margin, top + 10, w - 2 * style.margin, bottom - top - 14};
    typeset_page(page, preview, 1, {}, &fb_);
  }

  ui::draw_hint(fb_, t.hint_settings);
}

void App::open_settings(bool reading) {
  settings_reading_ = reading;
  settings_sel_ = 0;
  show(Screen::Settings);
}

void App::settings_activate() {
  const Row row = (settings_reading_ ? kReadingRows : kGeneralRows)[settings_sel_];
  if (row == Row::Battery) return show(Screen::Battery);
  if (row == Row::Stats) return show(Screen::Stats);
  if (row == Row::Light) return open_light(Screen::Settings);
  if (row == Row::Check) return open_check();
  if (row == Row::Updates) return open_updates();
  if (row == Row::ReadingPage) return open_settings(true);
  if (row == Row::MenuFont || row == Row::BookFont) {
    // The next typeface in the list, Literata after the last.
    char* name = row == Row::MenuFont ? settings_.menu_font : settings_.book_font;
    safe_fonts_ = false;
    const int next = (std::max(fonts_.find(name), 0) + 1) % fonts_.count();
    std::snprintf(name, sizeof(settings_.menu_font), "%s", fonts_.name(next));
  } else {
    set(settings_, row, (get(settings_, row) + 1) % value_count(row));
  }
  apply_fonts();
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
}

void App::apply_fonts() {
  book_fonts_ready_ = false;  // rendered when a page is first set (book_fonts())
  if (safe_fonts_) {
    // Started after a crash: Literata, built in, until a typeface is chosen again.
    ui::set_menu_fonts(literata_menu());
    return;
  }
  MenuFonts menu = literata_menu();
  const int menu_family = fonts_.find(settings_.menu_font);
  if (menu_family < 0 || !fonts_.menu(menu_family, menu)) menu = literata_menu();
  ui::set_menu_fonts(menu);
}

const BookFonts& App::book_fonts() {
  if (book_fonts_ready_) return book_fonts_;
  book_fonts_ready_ = true;
  const int px = text_px(settings_.text_size);
  book_fonts_ = literata_book(px);
  const int book_family = safe_fonts_ ? -1 : fonts_.find(settings_.book_font);
  if (book_family >= 0 && !fonts_.book(book_family, px, book_fonts_)) book_fonts_ = literata_book(px);
  return book_fonts_;
}

// ---- Adding books over Wi-Fi -------------------------------------------------

void App::open_transfer(bool setup) {
  transfer_setup_ = setup;
  if (board_.storage == nullptr && !setup) {
    transfer_state_ = TransferState::NoCard;
  } else if (board_.transfer == nullptr) {
    transfer_state_ = TransferState::Failed;
  } else {
    // Starting the radio takes a moment: say so first.
    transfer_state_ = TransferState::Off;
    show(Screen::Transfer);
    transfer_info_ = {};
    transfer_state_ = board_.transfer->start(transfer_info_) ? TransferState::Running : TransferState::Failed;
    if (setup) {
      // The QR code and address lead straight to the updates page.
      const size_t len = std::strlen(transfer_info_.address);
      std::snprintf(transfer_info_.address + len, sizeof(transfer_info_.address) - len, "/setup");
    } else if (transfer_state_ == TransferState::Running && board_.peers != nullptr && board_.peers->start()) {
      // Other readers can send books here too, alongside the upload page.
      receiver_.begin(*board_.peers, *board_.storage);
    }
    draw();
    queue_.submit();
    return;
  }
  show(Screen::Transfer);
}

void App::close_transfer() {
  bool received = receiver_.received() > 0;
  if (receiver_.state() != PeerReceiver::State::Off) {
    receiver_.end();
    board_.peers->stop();  // before the Wi-Fi it rides on
  }
  if (transfer_state_ == TransferState::Running) {
    TransferStatus st;
    board_.transfer->status(st);
    received = received || st.received > 0;
    board_.transfer->stop();
  }
  transfer_state_ = TransferState::Off;
  // The phone page may have changed notes or sent dictionaries.
  if (board_.storage != nullptr && !transfer_setup_) highlights_.load(*board_.storage);
  dictionaries_scanned_ = false;
  if (transfer_setup_) {
    transfer_setup_ = false;
    show(Screen::Updates);
  } else if (received) {
    reload_library();  // shows the home screen with the new books
  } else {
    show(Screen::Home);
  }
}

// A new version of Vellum arrived from the upload page and waits in the spare
// slot: say so, turn the radio off and restart into it. Boards that cannot
// restart (the simulators) carry on at the home screen.
void App::finish_update() {
  if (transfer_state_ == TransferState::Running) board_.transfer->stop();  // radio off first
  transfer_state_ = TransferState::Off;
  transfer_setup_ = false;
  restart_into_update();
}

void App::restart_into_update() {
  if (board_.storage != nullptr) board_.storage->write(kStatePath, "home\n", 5);
  update_state_.latest[0] = '\0';  // it is the running version now
  if (board_.storage != nullptr) save_update_state(*board_.storage, update_state_);
  save_battery();
  fb_.clear(Color::White);
  ui::draw_centered(fb_, ui::text_font(), fb_.height() / 2, text().update_done);
  queue_.submit_full();
  board_.power.restart();
  // Boards that cannot restart carry on at home.
  update_busy_ = UpdateBusy::No;
  show(Screen::Home);
}

// Escapes the characters a Wi-Fi QR code treats as syntax.
static void append_wifi_escaped(char* out, size_t size, size_t& len, const char* s) {
  for (; *s != '\0' && len + 2 < size; ++s) {
    if (std::strchr("\\;,:\"", *s) != nullptr) out[len++] = '\\';
    out[len++] = *s;
  }
  out[len] = '\0';
}

void App::draw_transfer() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, transfer_setup_ ? t.updates : t.add_books);
  draw_text(fb_, ui::label_font(), kSide * 64, kSubtitleBase, transfer_setup_ ? t.setup_lead : t.transfer_lead);

  const char* message = nullptr;
  switch (transfer_state_) {
    case TransferState::Off:
      message = t.starting;
      break;
    case TransferState::NoCard:
      message = t.no_card;
      break;
    case TransferState::Failed:
      message = t.no_wifi;
      break;
    case TransferState::Running:
      break;
  }
  if (message != nullptr) {
    ui::draw_wrapped(fb_, ui::text_font(), kSide, kTransferTop + 40, w - 2 * kSide, 3, message, false);
    ui::draw_hint(fb_, transfer_setup_ ? t.hint_setup : t.hint_transfer);
    return;
  }

  const TransferInfo& info = transfer_info_;
  const int col_x = kSide + kQrSize + 20;
  const int col_w = w - kSide - col_x;

  // Step 1: join the reader's Wi-Fi network.
  int y = kTransferTop;
  ui::draw_label(fb_, kSide, y, t.step_join);
  char join[160] = "WIFI:T:WPA;S:";
  size_t len = std::strlen(join);
  append_wifi_escaped(join, sizeof(join), len, info.network);
  std::snprintf(join + len, sizeof(join) - len, ";P:");
  len = std::strlen(join);
  append_wifi_escaped(join, sizeof(join), len, info.password);
  std::snprintf(join + len, sizeof(join) - len, ";;");
  ui::draw_qr(fb_, kSide, y + 12, kQrSize, join);
  draw_text(fb_, ui::label_font(), col_x * 64, y + 46, t.network);
  ui::draw_fitted(fb_, ui::large_font(), col_x, y + 80, col_w, info.network);
  draw_text(fb_, ui::label_font(), col_x * 64, y + 120, t.password);
  ui::draw_fitted(fb_, ui::large_font(), col_x, y + 154, col_w, info.password);

  // Step 2: open the upload page.
  y += kQrSize + 46;
  ui::draw_label(fb_, kSide, y, t.step_open);
  ui::draw_qr(fb_, kSide, y + 12, kQrSize, info.address);
  const char* address = info.address;
  if (std::strncmp(address, "http://", 7) == 0) address += 7;  // what people type
  ui::draw_fitted(fb_, ui::large_font(), col_x, y + 80, col_w, address);
  ui::draw_wrapped(fb_, ui::label_font(), col_x, y + 118, col_w, 3, t.scan_hint, false);

  // What has arrived so far.
  TransferStatus st;
  board_.transfer->status(st);
  y += kQrSize + 46;
  char line[160];
  using Update = TransferStatus::Update;
  if (transfer_setup_) {
    ui::draw_label(fb_, kSide, y, t.phone_label);
    if (st.update == Update::Receiving) {
      std::snprintf(line, sizeof(line), t.updating, st.update_percent);
    } else if (st.update == Update::NotVellum || st.update == Update::Broken) {
      std::snprintf(line, sizeof(line), "%s", st.update == Update::NotVellum ? t.update_not_vellum : t.update_broken);
    } else if (st.home_wifi_saved > 0 && board_.updater != nullptr) {
      char network[40];
      board_.updater->home_network(network, sizeof(network));
      std::snprintf(line, sizeof(line), t.wifi_saved, network);
    } else {
      std::snprintf(line, sizeof(line), "%s", t.waiting_phone);
    }
    ui::draw_fitted(fb_, ui::text_font(), kSide, y + 34, w - 2 * kSide, line);
    ui::draw_hint(fb_, t.hint_setup);
    return;
  }
  ui::draw_label(fb_, kSide, y, t.received_label);
  const int received = st.received + receiver_.received();
  if (received > 0) {
    char count[32];
    format_book_count(settings_.language, received, count, sizeof(count));
    ui::draw_right(fb_, ui::label_font(), w - kSide, y, count);
  }
  if (receiver_.busy()) {
    char name[150];
    std::snprintf(name, sizeof(name), "%s \xC2\xB7 %d%%", receiver_.receiving(), receiver_.percent());  // ·
    std::snprintf(line, sizeof(line), t.receiving, name);
  } else if (st.receiving[0] != '\0') {
    std::snprintf(line, sizeof(line), t.receiving, st.receiving);
  } else if (st.card_full || receiver_.card_full()) {
    std::snprintf(line, sizeof(line), "%s", t.card_full);
  } else if (st.received > 0) {
    std::snprintf(line, sizeof(line), "%s", st.last);
  } else if (receiver_.received() > 0) {
    std::snprintf(line, sizeof(line), "%s", receiver_.last());
  } else {
    std::snprintf(line, sizeof(line), "%s", t.waiting);
  }
  ui::draw_fitted(fb_, ui::text_font(), kSide, y + 34, w - 2 * kSide, line);
  if (receiver_.state() != PeerReceiver::State::Off) {
    ui::draw_wrapped(fb_, ui::label_font(), kSide, y + 74, w - 2 * kSide, 3, t.peer_hint, false);
  }

  ui::draw_hint(fb_, t.hint_transfer);
}

// ---- Updates -------------------------------------------------------------------

bool App::update_available() const { return is_newer_version(update_state_.latest, board_.version); }

void App::open_updates() {
  updates_sel_ = 0;
  update_error_ = false;
  show(Screen::Updates);
}

void App::record_update_check(UpdateResult result, const char* latest) {
  if (result != UpdateResult::Available && result != UpdateResult::UpToDate) return;
  textfile::copy(result == UpdateResult::Available ? latest : "", update_state_.latest, sizeof(update_state_.latest));
  update_state_.has_checked = board_.clock != nullptr && board_.clock->now(update_state_.checked);
  if (board_.storage != nullptr) save_update_state(*board_.storage, update_state_);
}

void App::check_for_update() {
  if (board_.updater == nullptr) return;
  update_busy_ = UpdateBusy::Checking;
  update_error_ = false;
  draw();
  queue_.submit();
  char latest[32] = "";
  const UpdateResult r = board_.updater->check(board_.version, latest, sizeof(latest));
  update_busy_ = UpdateBusy::No;
  record_update_check(r, latest);
  update_error_ = r != UpdateResult::Available && r != UpdateResult::UpToDate;
  update_result_ = r;
}

void App::install_update() {
  if (board_.updater == nullptr) return;
  update_busy_ = UpdateBusy::Installing;
  update_error_ = false;
  install_percent_ = 0;
  draw();
  queue_.submit();
  const UpdateResult r = board_.updater->install(
      [](void* ctx, int percent) {
        // Redraw every tenth: each refresh takes a moment of its own.
        auto* app = static_cast<App*>(ctx);
        if (percent / 10 == app->install_percent_ / 10) return;
        app->install_percent_ = percent;
        app->draw();
        app->queue_.submit();
      },
      this);
  if (r == UpdateResult::Installed) return restart_into_update();
  update_busy_ = UpdateBusy::No;
  update_error_ = true;
  update_result_ = r;
}

void App::updates_activate() {
  switch (updates_sel_) {
    case 0:
      if (update_available()) {
        install_update();
      } else {
        check_for_update();
      }
      break;
    case 1:
      settings_.auto_update = static_cast<AutoUpdate>((static_cast<int>(settings_.auto_update) + 1) % 3);
      if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
      break;
    default:
      open_transfer(true);
      break;
  }
}

void App::draw_updates() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.updates);
  char line[128];
  std::snprintf(line, sizeof(line), t.this_version, board_.version);
  draw_text(fb_, ui::label_font(), kSide * 64, kSubtitleBase, line);

  // Where things stand.
  ui::draw_label(fb_, kSide, kUpdateStatusLabelBase, t.status_label);
  char network[40] = "";
  if (board_.updater != nullptr) board_.updater->home_network(network, sizeof(network));
  const char* detail = nullptr;
  char checked[64] = "";
  if (update_state_.has_checked) {
    const DateTime& c = update_state_.checked;
    char when[24];
    std::snprintf(when, sizeof(when), "%04u-%02u-%02u ", unsigned{c.year}, unsigned{c.month}, unsigned{c.day});
    format_time(c, settings_.time_format, when + std::strlen(when), sizeof(when) - std::strlen(when));
    std::snprintf(checked, sizeof(checked), t.checked_at, when);
    detail = checked;
  }
  if (update_busy_ == UpdateBusy::Checking) {
    std::snprintf(line, sizeof(line), "%s", t.checking);
    detail = nullptr;
  } else if (update_busy_ == UpdateBusy::Installing) {
    std::snprintf(line, sizeof(line), t.installing, update_state_.latest, install_percent_);
    detail = nullptr;
  } else if (update_error_) {
    const char* why = t.install_failed;
    switch (update_result_) {
      case UpdateResult::NoNetwork:
        why = t.need_home_wifi;
        break;
      case UpdateResult::NoWifi:
        why = t.wifi_failed;
        break;
      case UpdateResult::NoServer:
        why = t.server_failed;
        break;
      default:
        break;
    }
    std::snprintf(line, sizeof(line), "%s", why);
  } else if (update_available()) {
    std::snprintf(line, sizeof(line), t.update_ready, update_state_.latest);
  } else if (network[0] == '\0') {
    std::snprintf(line, sizeof(line), "%s", t.need_home_wifi);
  } else if (update_state_.has_checked) {
    std::snprintf(line, sizeof(line), "%s", t.up_to_date);
  } else {
    std::snprintf(line, sizeof(line), "%s", t.not_checked);
  }
  const int y =
      ui::draw_wrapped(fb_, ui::text_font(), kSide, kUpdateStatusLabelBase + 36, w - 2 * kSide, 2, line, false);
  if (update_busy_ == UpdateBusy::Installing) {
    ui::draw_progress(fb_, kSide, y - 10, w - 2 * kSide, install_percent_, 100);
  } else if (detail != nullptr) {
    ui::draw_fitted(fb_, ui::label_font(), kSide, y - 4, w - 2 * kSide, detail);
  }

  // What can be done.
  char action[64];
  if (update_available()) {
    std::snprintf(action, sizeof(action), t.install_version, update_state_.latest);
  } else {
    std::snprintf(action, sizeof(action), "%s", t.check_now);
  }
  const char* names[kUpdateRows] = {action, t.automatic, t.home_wifi, t.from_file};
  const char* values[kUpdateRows] = {"", t.auto_names[static_cast<int>(settings_.auto_update)],
                                     network[0] != '\0' ? network : t.not_set, ""};
  for (int i = 0; i < kUpdateRows; ++i) {
    const Rect r = row_rect(fb_, kUpdateRowsTop + i * kUpdateRowHeight, kUpdateRowHeight);
    const bool selected = i == updates_sel_;
    if (i > 0 && !selected && updates_sel_ != i - 1) dotted_rule(fb_, r.y);
    add_hit(r, i);
    const Color fg = highlight(fb_, r, selected);
    const int base = r.y + 32;
    draw_text(fb_, ui::text_font(), (r.x + kPad) * 64, base, names[i], fg);
    char value[64];
    std::snprintf(value, sizeof(value), "%s%s%s", values[i], values[i][0] != '\0' && i >= 2 ? " " : "",
                  i >= 2 ? "\xE2\x80\xBA" : "");  // ›
    const int name_w = text_width(ui::text_font(), names[i]) / 64;
    const int right = r.x + r.w - kPad;
    const int room = right - (r.x + kPad + name_w + 16);
    const int value_w = text_width(ui::text_font(), value) / 64;
    if (value_w <= room) {
      ui::draw_right(fb_, ui::text_font(), right, base, value, fg);
    } else if (room > 0) {
      ui::draw_fitted(fb_, ui::text_font(), right - room, base, room, value, fg);  // a long network name
    }
  }

  // What the automatic setting does, and what it lacks.
  int help_y = kUpdateRowsTop + kUpdateRows * kUpdateRowHeight + 34;
  help_y = ui::draw_wrapped(fb_, ui::label_font(), kSide, help_y, w - 2 * kSide, 3,
                            t.auto_help[static_cast<int>(settings_.auto_update)], false);
  DateTime now;
  const bool clock_set = board_.clock != nullptr && board_.clock->now(now);
  if (settings_.auto_update != AutoUpdate::Off && (network[0] == '\0' || !clock_set)) {
    ui::draw_wrapped(fb_, ui::label_font(), kSide, help_y + 6, w - 2 * kSide, 3, t.night_needs, false);
  }
  ui::draw_hint(fb_, t.hint_updates);
}

// ---- Updates at night ----------------------------------------------------------

void App::schedule_night_check() {
  uint32_t seconds = 0;
  DateTime now;
  if (settings_.auto_update != AutoUpdate::Off && board_.updater != nullptr && board_.clock != nullptr &&
      board_.clock->now(now)) {
    char network[40];
    board_.updater->home_network(network, sizeof(network));
    if (network[0] != '\0') seconds = seconds_until_hour(now, kNightHour);
  }
  board_.power.set_wake_timer(seconds);
}

void App::night_update() {
  // The screen still shows the sleep cover: leave it as it is.
  asleep_ = true;
  before_sleep_ = resume_reader_ ? Screen::Reader : Screen::Home;
  if (update_state_.night_restart) {
    // The version installed a moment ago started: back to sleep.
    update_state_.night_restart = false;
    if (board_.storage != nullptr) save_update_state(*board_.storage, update_state_);
  } else if (settings_.auto_update != AutoUpdate::Off && board_.updater != nullptr &&
             !battery_below(kNightUpdatePercent)) {
    char latest[32] = "";
    const UpdateResult r = board_.updater->check(board_.version, latest, sizeof(latest));
    record_update_check(r, latest);
    if (r == UpdateResult::Available && settings_.auto_update == AutoUpdate::AtNight && board_.storage != nullptr) {
      update_state_.night_restart = true;  // the new version sleeps again after its first start
      save_update_state(*board_.storage, update_state_);
      if (board_.updater->install(nullptr, nullptr) == UpdateResult::Installed) {
        update_state_.latest[0] = '\0';
        save_update_state(*board_.storage, update_state_);
        board_.power.restart();
        return;
      }
      update_state_.night_restart = false;
      save_update_state(*board_.storage, update_state_);
    }
  }
  schedule_night_check();
  board_.power.sleep();  // a deep-sleeping board restarts from here on waking
}

// ---- Sending to another reader ---------------------------------------------------

bool App::can_send() const {
  return board_.peers != nullptr && board_.storage != nullptr && reading_ >= 0 &&
         library_.entry(reading_).builtin == nullptr;
}

void App::open_send() {
  send_sel_ = 0;
  radio_failed_ = !board_.peers->start();
  if (!radio_failed_) sender_.begin(*board_.peers, *board_.storage, library_.entry(reading_).path);
  show(Screen::Send);
}

void App::close_send() {
  sender_.cancel();
  if (!radio_failed_) board_.peers->stop();
  radio_failed_ = false;
}

bool App::send_activate() {
  if (radio_failed_) return false;
  switch (sender_.state()) {
    case PeerSender::State::Looking:
      if (sender_.reader_count() > 0) sender_.send_to(send_sel_);
      return true;
    case PeerSender::State::Sent:
      close_send();
      show(Screen::Reader);
      return false;
    case PeerSender::State::Failed:
      send_sel_ = 0;
      sender_.begin(*board_.peers, *board_.storage, library_.entry(reading_).path);  // try again
      return true;
    default:
      return false;  // sending: Home cancels
  }
}

bool App::peers_ticking() const { return (screen_ == Screen::Send && sender_.needs_ticks()) || receiver_.busy(); }

bool App::service_peers(bool tick) {
  if (board_.peers == nullptr) return false;
  const bool sending = screen_ == Screen::Send && !radio_failed_;
  bool changed = false;
  static PeerPacket packet;  // too large for a small task stack
  while (board_.peers->receive(packet)) {
    if (sending) changed |= sender_.on_packet(packet);
    if (receiver_.state() != PeerReceiver::State::Off) changed |= receiver_.on_packet(packet);
  }
  if (tick) {
    if (sending) changed |= sender_.tick();
    changed |= receiver_.tick();
  }
  return changed && (screen_ == Screen::Send || screen_ == Screen::Transfer);
}

void App::draw_send() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.send_title);
  ui::draw_fitted(fb_, ui::label_font(), kSide, kSubtitleBase, w - 2 * kSide, library_.entry(reading_).title);

  const int top = kListTop + 30;
  char line[160];
  const char* message = nullptr;
  const char* hint = t.hint_retry;
  switch (radio_failed_ ? PeerSender::State::Failed : sender_.state()) {
    case PeerSender::State::Idle:
    case PeerSender::State::Looking: {
      ui::draw_label(fb_, kSide, top, t.nearby_label);
      hint = t.hint_send;
      if (sender_.reader_count() == 0) {
        message = t.looking;
        break;
      }
      for (int i = 0; i < sender_.reader_count(); ++i) {
        const Rect r = row_rect(fb_, top + 14 + i * kLinkHeight, kLinkHeight);
        add_hit(r, i);
        draw_link(fb_, r, sender_.reader(i).name, i == send_sel_);
      }
      break;
    }
    case PeerSender::State::Offering:
    case PeerSender::State::Sending:
    case PeerSender::State::Finishing: {
      std::snprintf(line, sizeof(line), t.sending_to, sender_.target_name());
      ui::draw_fitted(fb_, ui::text_font(), kSide, top + 20, w - 2 * kSide, line);
      // Progress: a dotted track filled solid so far, and the share in figures.
      const int bar_y = top + 60;
      const int filled = (w - 2 * kSide) * sender_.percent() / 100;
      dotted_rule(fb_, bar_y);
      fb_.fill_rect({kSide, bar_y - 2, filled, 5}, Color::Black);
      char percent[8];
      std::snprintf(percent, sizeof(percent), "%d%%", sender_.percent());
      ui::draw_right(fb_, ui::screen_title_font(), w - kSide, bar_y + 60, percent);
      hint = t.hint_sending;
      break;
    }
    case PeerSender::State::Sent:
      std::snprintf(line, sizeof(line), t.sent_to, sender_.target_name());
      message = line;
      hint = t.hint_sent;
      break;
    case PeerSender::State::Failed:
      switch (radio_failed_ ? PeerSender::Error::None : sender_.error()) {
        case PeerSender::Error::None:
          message = t.no_wifi;
          break;
        case PeerSender::Error::NoAnswer:
          message = t.send_no_answer;
          break;
        case PeerSender::Error::Refused:
          message = t.send_refused;
          break;
        case PeerSender::Error::CardFull:
          message = t.send_card_full;
          break;
        case PeerSender::Error::Corrupt:
          message = t.send_corrupt;
          break;
        case PeerSender::Error::CannotRead:
          message = t.send_unreadable;
          break;
      }
      break;
  }
  if (message != nullptr) {
    ui::draw_wrapped(fb_, ui::text_font(), kSide, top + 44, w - 2 * kSide, 4, message, false);
  }
  ui::draw_hint(fb_, hint);
}

// ---- Contents and bookmarks ---------------------------------------------------

void App::open_contents() {
  contents_mark_count_ = library_.bookmarks_of(reading_, contents_marks_, Library::kMaxBookmarks);
  int marked[Highlights::kMax];
  const int n = highlights_.of_book(library_.entry(reading_).path, -1, marked, Highlights::kMax);
  contents_hl_row_count_ = 0;
  for (int k = 0; k < n; ++k) {
    contents_hl_rows_[contents_hl_row_count_++] = static_cast<int16_t>(marked[k] * 2);
    if (highlights_.at(marked[k]).note[0] != '\0') {
      contents_hl_rows_[contents_hl_row_count_++] = static_cast<int16_t>(marked[k] * 2 + 1);
    }
  }
  contents_sel_ = 0;
  show(Screen::Contents);
}

int App::contents_rows_per_page() const { return (fb_.height() - kContentsTop - kContentsBottom) / kContentsRowHeight; }

int App::contents_row_count() const {
  const Document& doc = *reader_.document();
  const int chapters = doc.toc_count() > 0 ? doc.toc_count() : doc.chapter_count();
  return (can_send() ? 5 : 4) + (contents_mark_count_ > 0 ? 1 + contents_mark_count_ : 0) +
         (contents_hl_row_count_ > 0 ? 1 + contents_hl_row_count_ : 0) + 1 + chapters;
}

bool App::contents_skipped(int row) const {
  int index;
  const ContentsRow kind = contents_row(row, index);
  return kind == ContentsRow::BookmarksLabel || kind == ContentsRow::HighlightsLabel ||
         kind == ContentsRow::ChaptersLabel || kind == ContentsRow::Note;
}

App::ContentsRow App::contents_row(int i, int& index) const {
  index = 0;
  if (i == 0) return ContentsRow::Back;
  if (i == 1) return ContentsRow::Mark;
  if (i == 2) return ContentsRow::Light;
  if (i == 3) return ContentsRow::LookUp;
  if (can_send()) {
    if (i == 4) return ContentsRow::Send;
    --i;
  }
  i -= 4;
  if (contents_mark_count_ > 0) {
    if (i == 0) return ContentsRow::BookmarksLabel;
    if (i <= contents_mark_count_) {
      index = i - 1;
      return ContentsRow::Bookmark;
    }
    i -= 1 + contents_mark_count_;
  }
  if (contents_hl_row_count_ > 0) {
    if (i == 0) return ContentsRow::HighlightsLabel;
    if (i <= contents_hl_row_count_) {
      const int code = contents_hl_rows_[i - 1];
      index = code / 2;  // the highlight, for its note too
      return code % 2 == 0 ? ContentsRow::Highlight : ContentsRow::Note;
    }
    i -= 1 + contents_hl_row_count_;
  }
  if (i == 0) return ContentsRow::ChaptersLabel;
  index = i - 1;
  return ContentsRow::Chapter;
}

int App::current_chapter_row() const {
  const Document& doc = *reader_.document();
  if (doc.toc_count() == 0) return reader_.chapter();
  int current = -1;
  for (int j = 0; j < doc.toc_count(); ++j) {
    const TocEntry& e = doc.toc(j);
    const BookPos p{e.chapter, {e.paragraph == TocEntry::kUnknown ? 0 : e.paragraph, 0}};
    if (reader_.compare_to_page(p) <= 0) current = j;  // the last one that starts by this page
  }
  return current;
}

int App::page_bookmark() const {
  if (reading_ < 0) return -1;
  int marks[Library::kMaxBookmarks];
  const int n = library_.bookmarks_of(reading_, marks, Library::kMaxBookmarks);
  for (int k = 0; k < n; ++k) {
    if (reader_.compare_to_page(library_.bookmark(marks[k]).position) == 0) return marks[k];
  }
  return -1;
}

void App::jump_to(BookPos pos) {
  Document& doc = *reader_.document();
  const BookPos before = reader_.position();
  const ReadingStyle style = reading_style(settings_, &book_fonts());
  if (!reader_.open(doc, reader_.id(), style, pos, fb_)) reader_.open(doc, reader_.id(), style, before, fb_);
  BookState& state = library_.state(reading_);
  state.position = reader_.position();
  state.percent = static_cast<uint8_t>(reader_.percent());
  save_progress();
  show(Screen::Reader);
}

void App::contents_activate() {
  int index;
  switch (contents_row(contents_sel_, index)) {
    case ContentsRow::Back:
      return show(Screen::Reader);
    case ContentsRow::Mark: {
      const int mark = page_bookmark();
      if (mark >= 0) {
        library_.remove_bookmark(mark);
      } else {
        char snippet[sizeof(Bookmark::snippet)];
        reader_.snippet(snippet, sizeof(snippet));
        library_.add_bookmark(reading_, reader_.position(), static_cast<uint8_t>(reader_.percent()), snippet);
      }
      if (board_.storage != nullptr) library_.save_bookmarks(*board_.storage);
      return show(Screen::Reader);
    }
    case ContentsRow::Light:
      return open_light(Screen::Reader);
    case ContentsRow::LookUp:
      return open_word_pick();
    case ContentsRow::Send:
      return open_send();
    case ContentsRow::Bookmark:
      return jump_to(library_.bookmark(contents_marks_[index]).position);
    case ContentsRow::Highlight:
    case ContentsRow::Note:
      return jump_to(highlights_.at(index).begin);
    case ContentsRow::Chapter: {
      Document& doc = *reader_.document();
      if (doc.toc_count() == 0) return jump_to({static_cast<uint16_t>(index), {}});
      const TocEntry& e = doc.toc(index);
      if (e.paragraph == TocEntry::kUnknown) {
        Chapter ch;
        doc.load_chapter(e.chapter, ch);  // finds where the entry points
      }
      return jump_to({e.chapter, {e.paragraph == TocEntry::kUnknown ? 0 : e.paragraph, 0}});
    }
    case ContentsRow::BookmarksLabel:
    case ContentsRow::HighlightsLabel:
    case ContentsRow::ChaptersLabel:
      return;
  }
}

void App::draw_contents() {
  const int w = fb_.width();
  const Strings& t = text();
  const Document& doc = *reader_.document();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.contents);

  const int per_page = contents_rows_per_page();
  const int count = contents_row_count();
  const int pages = (count + per_page - 1) / per_page;
  const int page = contents_sel_ / per_page;
  int title_w = w - 2 * kSide;
  if (pages > 1) {
    char label[48];
    std::snprintf(label, sizeof(label), t.page_of, page + 1, pages);
    ui::draw_right(fb_, ui::label_font(), w - kSide, kSubtitleBase, label);
    title_w -= text_width_px(ui::label_font(), label) + 16;
  }
  ui::draw_fitted(fb_, ui::label_font(), kSide, kSubtitleBase, title_w, library_.entry(reading_).title);
  char chapter_left[24], book_left[24];
  if (time_left(chapter_left, book_left, sizeof(chapter_left))) {
    char line[128];
    std::snprintf(line, sizeof(line), t.contents_left, chapter_left, book_left);
    ui::draw_fitted(fb_, ui::label_font(), kSide, kContentsLeftBase, w - 2 * kSide, line);
  }

  const int mark = page_bookmark();
  const int current = current_chapter_row();
  const int first = page * per_page;
  const int last = std::min(count, first + per_page);
  ContentsRow prev_kind = ContentsRow::Back;
  for (int i = first; i < last; ++i) {
    int index;
    const ContentsRow kind = contents_row(i, index);
    const int y = kContentsTop + (i - first) * kContentsRowHeight;
    const Rect r = row_rect(fb_, y, kContentsRowHeight);
    const int base = y + 33;
    const bool selected = i == contents_sel_;
    const bool listed = kind == ContentsRow::Bookmark || kind == ContentsRow::Chapter || kind == ContentsRow::Highlight;
    // A note belongs to the highlight above it: no rule between them.
    const ContentsRow group = prev_kind == ContentsRow::Note ? ContentsRow::Highlight : prev_kind;
    if (listed && i > first && group == kind && !selected && contents_sel_ != i - 1) dotted_rule(fb_, y);
    prev_kind = kind;
    switch (kind) {
      case ContentsRow::BookmarksLabel:
      case ContentsRow::HighlightsLabel:
      case ContentsRow::ChaptersLabel:
        ui::draw_label(fb_, kSide, base + 6,
                       kind == ContentsRow::BookmarksLabel    ? t.bookmarks_label
                       : kind == ContentsRow::HighlightsLabel ? t.highlights_label
                                                              : t.chapters_label);
        continue;
      case ContentsRow::Back:
      case ContentsRow::Mark:
      case ContentsRow::Light:
        draw_link(fb_, {r.x, r.y + 2, r.w, kLinkHeight},
                  kind == ContentsRow::Back    ? t.back_to_book
                  : kind == ContentsRow::Light ? t.front_light
                  : mark >= 0                  ? t.remove_bookmark
                                               : t.add_bookmark,
                  selected);
        break;
      case ContentsRow::LookUp:
      case ContentsRow::Send:
        draw_link(fb_, {r.x, r.y + 2, r.w, kLinkHeight}, kind == ContentsRow::Send ? t.send_book : t.look_up_word,
                  selected);
        break;
      case ContentsRow::Highlight: {
        const Highlight& h = highlights_.at(index);
        const Color fg = highlight(fb_, r, selected);
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", h.percent);
        const int right = r.x + r.w - kPad;
        ui::draw_right(fb_, ui::label_font(), right, base, percent, fg);
        const int x = r.x + kPad;
        // The passage's first words, underlined as on the page.
        const int shown = std::min(text_width_px(ui::small_italic_font(), h.text), right - 56 - x);
        ui::draw_fitted(fb_, ui::small_italic_font(), x, base, right - 56 - x, h.text, fg);
        fb_.fill_rect({x, base + 5, shown, 1}, fg);
        break;
      }
      case ContentsRow::Note: {
        // The note from the phone, set in from its highlight behind a bar.
        const Highlight& h = highlights_.at(index);
        const int x = r.x + kPad + kIndent;
        fb_.fill_rect({x - 10, y + 6, 2, kContentsRowHeight - 16}, Color::Black);
        ui::draw_wrapped(fb_, ui::label_font(), x, y + 22, r.x + r.w - kPad - x, 2, h.note, false);
        break;
      }
      case ContentsRow::Bookmark: {
        const Bookmark& b = library_.bookmark(contents_marks_[index]);
        const Color fg = highlight(fb_, r, selected);
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", b.percent);
        const int right = r.x + r.w - kPad;
        ui::draw_right(fb_, ui::label_font(), right, base, percent, fg);
        const int x = r.x + kPad;
        ui::draw_fitted(fb_, ui::small_italic_font(), x, base, right - 56 - x, b.snippet, fg);
        break;
      }
      case ContentsRow::Chapter: {
        const Color fg = highlight(fb_, r, selected);
        char numbered[32];
        const char* title = numbered;
        int depth = 0;
        if (doc.toc_count() > 0) {
          title = doc.toc(index).title;
          depth = std::min<int>(doc.toc(index).depth, 3);
        } else {
          std::snprintf(numbered, sizeof(numbered), t.chapter_n, index + 1);
        }
        const int x = r.x + kPad + depth * kIndent;
        const int right = r.x + r.w - kPad;
        ui::draw_fitted(fb_, ui::text_font(), x, base, right - 24 - x, title, fg);
        if (index == current) fb_.fill_round_rect({right - 8, base - 12, 8, 8}, 4, fg);  // you are here
        break;
      }
    }
    add_hit(r, i);
  }
  ui::draw_hint(fb_, t.hint_list);
}

// ---- Battery -------------------------------------------------------------------

bool App::record_battery(BatterySample::Kind kind) {
  DateTime now;
  const int level = board_.power.battery_percent();
  if (board_.clock == nullptr || !board_.clock->now(now) || level < 0) return false;
  BatterySample s;
  s.time = seconds_since_2000(now);
  s.percent = static_cast<uint8_t>(level > 100 ? 100 : level);
  s.kind = kind;
  s.charging = board_.power.charging();
  battery_.add(s);
  return true;
}

void App::save_battery() {
  if (board_.storage == nullptr || battery_.count() == 0) return;
  battery_.save(*board_.storage);
  battery_saved_at_ = battery_.latest()->time;
}

void App::sample_battery() {
  const BatterySample* last = battery_.latest();
  const bool was_charging = last != nullptr && last->charging;
  const uint32_t last_time = last != nullptr ? last->time : 0;
  DateTime now;
  if (board_.clock == nullptr || !board_.clock->now(now)) return;
  const uint32_t t = seconds_since_2000(now);
  const bool charging = board_.power.charging();
  if (last != nullptr && t >= last_time && t - last_time < BatteryLog::kIntervalS && charging == was_charging) return;
  if (!record_battery(BatterySample::Kind::Awake)) return;
  // Plugging in or out is worth a card write; otherwise once an hour.
  if (charging != was_charging || t - battery_saved_at_ >= BatteryLog::kSaveIntervalS) save_battery();
}

void App::draw_battery() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.battery);
  const int level = board_.power.battery_percent();
  char big[16] = "\xE2\x80\x93";  // –
  if (level >= 0) std::snprintf(big, sizeof(big), "%d%%", level);
  ui::draw_right(fb_, ui::screen_title_font(), w - kSide, kTitleBase, big);
  draw_text(fb_, ui::label_font(), kSide * 64, kSubtitleBase, board_.power.charging() ? t.charging : t.on_battery);

  DateTime now;
  if (board_.clock == nullptr || !board_.clock->now(now)) {
    ui::draw_wrapped(fb_, ui::text_font(), kSide, kChartLabelBase + 30, w - 2 * kSide, 6, t.clock_unset, false);
    ui::draw_hint(fb_, t.hint_battery);
    return;
  }

  // The last seven days: charge against time, solid while in use or
  // charging, dotted while asleep.
  ui::draw_label(fb_, kSide, kChartLabelBase, t.history_label);
  const int left = kSide + kChartAxis;
  const int right = w - kSide;
  const uint32_t end = seconds_since_2000(now);
  const uint32_t span = kChartDays * 86400u;
  const uint32_t start = end > span ? end - span : 0;
  auto x_of = [&](uint32_t time) {
    if (time <= start) return left;
    return left + static_cast<int>(uint64_t{time - start} * (right - left) / span);
  };
  auto y_of = [&](int percent) { return kChartBottom - percent * (kChartBottom - kChartTop) / 100; };
  for (int p : {0, 50, 100}) {
    const int y = y_of(p);
    for (int x = left; x < right; x += 4) fb_.set_pixel(x, y, Color::Black);
    char label[8];
    std::snprintf(label, sizeof(label), "%d", p);
    ui::draw_right(fb_, ui::label_font(), left - 8, y + 6, label);
  }
  // Midnights, labelled with the day of the month that starts there.
  for (uint32_t day = start / 86400 + 1; day * 86400 <= end; ++day) {
    const int x = x_of(day * 86400);
    fb_.vline(x, kChartBottom, 6, Color::Black);
    uint16_t year;
    uint8_t month, mday;
    date_from_days(day, year, month, mday);
    char label[4];
    std::snprintf(label, sizeof(label), "%u", mday);
    if (x + 4 < right) draw_text(fb_, ui::label_font(), (x + 4) * 64, kChartBottom + 22, label);
  }
  for (int i = 1; i < battery_.count(); ++i) {
    const BatterySample& a = battery_.at(i - 1);
    const BatterySample& b = battery_.at(i);
    if (b.time < start || b.time > end) continue;
    draw_line(fb_, x_of(a.time), y_of(a.percent), x_of(b.time), y_of(b.percent), a.kind == BatterySample::Kind::Sleep);
  }
  if (const BatterySample* last = battery_.latest(); last != nullptr && last->time >= start) {
    fb_.fill_round_rect({x_of(last->time) - 4, y_of(last->percent) - 3, 9, 9}, 4, Color::Black);  // now
  }

  // What the history says so far.
  ui::draw_label(fb_, kSide, kEstimatesBase, t.estimates_label);
  const BatteryEstimate e = battery_.estimate();
  char left_text[48], reading[48], asleep[48], number[16];
  std::snprintf(left_text, sizeof(left_text), "%s", t.measuring);
  std::snprintf(reading, sizeof(reading), "%s", t.measuring);
  std::snprintf(asleep, sizeof(asleep), "%s", t.measuring);
  if (e.hours_left >= 0) format_time_left(settings_.language, e.hours_left, left_text, sizeof(left_text));
  if (e.awake_tenths_per_hour >= 0) {
    format_tenths(settings_.language, e.awake_tenths_per_hour, number, sizeof(number));
    std::snprintf(reading, sizeof(reading), t.per_hour, number);
  }
  if (e.asleep_tenths_per_day >= 0) {
    format_tenths(settings_.language, e.asleep_tenths_per_day, number, sizeof(number));
    std::snprintf(asleep, sizeof(asleep), t.per_day, number);
  }
  const char* names[] = {t.time_left, t.while_reading, t.while_asleep};
  const char* values[] = {left_text, reading, asleep};
  for (int i = 0; i < 3; ++i) {
    const int y = kEstimatesBase + 14 + i * kSettingRowHeight;
    if (i > 0) dotted_rule(fb_, y);
    draw_text(fb_, ui::text_font(), kSide * 64, y + 28, names[i]);
    ui::draw_right(fb_, ui::text_font(), w - kSide, y + 28, values[i]);
  }
  ui::draw_hint(fb_, t.hint_battery);
}

// ---- Reading stats -------------------------------------------------------------

uint32_t App::steady_ms() const {
  if (const uint32_t ms = board_.input.now_ms(); ms != 0) return ms;
  // Only differences matter, so the clock's milliseconds may wrap.
  DateTime now;
  if (board_.clock != nullptr && board_.clock->now(now)) return seconds_since_2000(now) * 1000u;
  return 0;
}

void App::stop_reading() {
  if (!stats_.running()) return;
  stats_.stop(steady_ms(), today(board_.clock));
  if (stats_.dirty() && board_.storage != nullptr) stats_.save(*board_.storage);
}

bool App::time_left(char* chapter, char* book, size_t size) const {
  if (reading_ < 0 || reader_.document() == nullptr) return false;
  const int speed = stats_.pages_per_hour(library_.entry(reading_).path);
  if (speed <= 0) return false;
  format_duration(settings_.language, minutes_for_pages(reader_.pages_left_in_chapter(), speed), chapter, size);
  format_duration(settings_.language, minutes_for_pages(reader_.pages_left_in_book(), speed), book, size);
  return true;
}

void App::draw_stats() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  const Language lang = settings_.language;
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.reading_stats);
  char value[48];

  const int day = today(board_.clock);
  int y = kStatRowsTop;
  if (day < 0) {
    // Without the clock there are no days: say why, and show the rest.
    y = ui::draw_wrapped(fb_, ui::text_font(), kSide, kTodayLabelBase, w - 2 * kSide, 5, t.stats_clock, false) + 6;
  } else {
    // Today, in large figures.
    ui::draw_label(fb_, kSide, kTodayLabelBase, t.today_label);
    format_duration(lang, static_cast<int>(stats_.seconds_between(day, day) / 60), value, sizeof(value));
    draw_text(fb_, ui::screen_title_font(), kSide * 64, kTodayBase, value);
    format_page_count(lang, static_cast<int>(stats_.pages_between(day, day)), value, sizeof(value));
    ui::draw_right(fb_, ui::text_font(), w - kSide, kTodayBase, value);

    // The last seven days as bars of minutes, today on the right.
    ui::draw_label(fb_, kSide, kWeekLabelBase, t.week_label);
    const int first = day - (kChartDays - 1);
    format_duration(lang, static_cast<int>(stats_.seconds_between(first, day) / 60), value, sizeof(value));
    ui::draw_right(fb_, ui::label_font(), w - kSide, kWeekLabelBase, value);
    int minutes[kChartDays];
    int top = kMinScaleMinutes;
    for (int i = 0; i < kChartDays; ++i) {
      minutes[i] = static_cast<int>(stats_.seconds_between(first + i, first + i) / 60);
      top = std::max(top, minutes[i]);
    }
    const int slot = (w - 2 * kSide) / kChartDays;
    const int room = kBarsBottom - kBarsTop - 22;  // the figure over the tallest bar
    dotted_rule(fb_, kBarsBottom + 1);
    for (int i = 0; i < kChartDays; ++i) {
      const int cx = kSide + i * slot + slot / 2;
      const int bar_h = minutes[i] * room / top;
      if (minutes[i] > 0) {
        // A solid bar with rounded shoulders, its minutes on top.
        fb_.fill_round_rect({cx - kBarW / 2, kBarsBottom - bar_h, kBarW, bar_h + 1}, std::min(4, bar_h / 2),
                            Color::Black);
        fb_.fill_rect({cx - kBarW / 2, kBarsBottom - std::min(4, bar_h), kBarW, std::min(4, bar_h) + 1}, Color::Black);
        std::snprintf(value, sizeof(value), "%d", minutes[i]);
        const int tw = text_width_px(ui::label_font(), value);
        draw_text(fb_, ui::label_font(), (cx - tw / 2) * 64, kBarsBottom - bar_h - 8, value);
      } else {
        fb_.fill_rect({cx - kBarW / 2, kBarsBottom - 1, kBarW, 2}, Color::Black);  // a day without reading
      }
      // The day's name; today's in a dark pill.
      const char* name = t.weekdays[(first + i + 5) % 7];  // 2000-01-01 was a Saturday
      const int tw = text_width_px(ui::label_font(), name);
      const int base = kBarsBottom + 26;
      Color fg = Color::Black;
      if (i == kChartDays - 1) {
        fb_.fill_round_rect({cx - tw / 2 - 8, base - 18, tw + 16, 25}, 12, Color::Black);
        fg = Color::White;
      }
      draw_text(fb_, ui::label_font(), (cx - tw / 2) * 64, base, name, fg);
    }
  }

  // Totals and speed.
  uint16_t year;
  uint8_t month, mday;
  date_from_days(day < 0 ? 0 : static_cast<uint32_t>(day), year, month, mday);
  char month_text[32], speed_text[48], all_text[32];
  format_duration(lang, static_cast<int>(stats_.seconds_between(day - (mday - 1), day) / 60), month_text,
                  sizeof(month_text));
  uint32_t all_seconds = 0;
  for (int i = 0; i < stats_.book_count(); ++i) all_seconds += stats_.book_by_recent(i).seconds;
  format_duration(lang, static_cast<int>(all_seconds / 60), all_text, sizeof(all_text));
  std::snprintf(speed_text, sizeof(speed_text), "%s", t.measuring);
  if (const int speed = stats_.pages_per_hour(); speed > 0)
    std::snprintf(speed_text, sizeof(speed_text), t.pages_an_hour, speed);
  const char* names[] = {t.this_month, t.reading_speed, t.all_time};
  const char* values[] = {month_text, speed_text, all_text};
  for (int i = day < 0 ? 1 : 0; i < 3; ++i) {
    if (i > (day < 0 ? 1 : 0)) dotted_rule(fb_, y);
    draw_text(fb_, ui::text_font(), kSide * 64, y + 28, names[i]);
    ui::draw_right(fb_, ui::text_font(), w - kSide, y + 28, values[i]);
    y += kSettingRowHeight;
  }

  // The books read lately: title, then time and pages.
  ui::draw_label(fb_, kSide, y + 36, t.books_label);
  y += 48;
  int shown = 0;
  for (int i = 0; i < stats_.book_count() && y + kStatsBookHeight <= h - 48; ++i) {
    const ReadingStats::BookTotal& b = stats_.book_by_recent(i);
    const char* title = nullptr;
    for (int k = 0; k < library_.count() && title == nullptr; ++k) {
      if (std::strcmp(library_.entry(k).path, b.path) == 0) title = library_.entry(k).title;
    }
    if (title == nullptr || (b.seconds < 60 && b.pages == 0)) continue;  // gone from the card, or barely opened
    if (shown > 0) dotted_rule(fb_, y);
    ui::draw_fitted(fb_, ui::small_italic_font(), kSide, y + 25, w - 2 * kSide, title);
    char time_text[24], pages_text[24], line[64];
    format_duration(lang, static_cast<int>(b.seconds / 60), time_text, sizeof(time_text));
    format_page_count(lang, static_cast<int>(b.pages), pages_text, sizeof(pages_text));
    std::snprintf(line, sizeof(line), "%s \xC2\xB7 %s", time_text, pages_text);  // ·
    draw_text(fb_, ui::label_font(), kSide * 64, y + 45, line);
    y += kStatsBookHeight;
    ++shown;
  }
  if (shown == 0) ui::draw_wrapped(fb_, ui::text_font(), kSide, y + 24, w - 2 * kSide, 3, t.nothing_read, false);
  ui::draw_hint(fb_, t.hint_battery);
}

// ---- Front light --------------------------------------------------------------

void App::apply_light() {
  if (board_.frontlight == nullptr) return;
  board_.frontlight->set(static_cast<uint8_t>(settings_.light * 255 / kLightSteps),
                         static_cast<uint8_t>(settings_.warmth * 255 / kLightSteps));
}

void App::open_light(Screen back) {
  light_back_ = back;
  light_sel_ = 0;
  show(Screen::Light);
}

void App::close_light() {
  if (board_.storage != nullptr) save_settings(*board_.storage, settings_);
  show(light_back_);
}

void App::draw_light() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.front_light);

  const int x0 = kSide + kKnob / 2;
  const int track_w = w - 2 * kSide - kKnob;
  for (int i = 0; i < 2; ++i) {
    const int y = kLightTop + i * kLightBlock;
    const Rect r = row_rect(fb_, y, kLightBlock - 12);
    add_hit(r, i);
    if (light_sel_ == i) {
      fb_.fill_round_rect(r, 12, Color::Black);
      fb_.fill_round_rect({r.x + 3, r.y + 3, r.w - 6, r.h - 6}, 9, Color::White);
    }
    const int value = i == 0 ? settings_.light : settings_.warmth;
    char label[16];
    if (i == 0 && value == 0) {
      std::snprintf(label, sizeof(label), "%s", t.off);
    } else {
      std::snprintf(label, sizeof(label), "%d%%", value * 100 / kLightSteps);
    }
    draw_text(fb_, ui::text_font(), kSide * 64, y + 36, i == 0 ? t.brightness : t.warmth);
    ui::draw_right(fb_, ui::text_font(), w - kSide, y + 36, label);
    const int track_y = y + kLightTrack;
    ui::draw_progress(fb_, x0, track_y, track_w, value, kLightSteps);
    const int knob_x = x0 + value * track_w / kLightSteps;
    fb_.fill_round_rect({knob_x - kKnob / 2, track_y - kKnob / 2, kKnob, kKnob}, kKnob / 2, Color::Black);
    if (i == 1) {
      draw_text(fb_, ui::label_font(), kSide * 64, track_y + 26, t.cool);
      ui::draw_right(fb_, ui::label_font(), w - kSide, track_y + 26, t.warm);
    }
  }
  const Rect done = row_rect(fb_, kLightTop + 2 * kLightBlock, kLinkHeight);
  draw_link(fb_, done, t.done, light_sel_ == 2);
  add_hit(done, 2);
  ui::draw_hint(fb_, t.hint_light);
}

bool App::light_tap(int x, int y) {
  const int item = hit_at(x, y);
  if (item < 0) return false;
  if (item == 2) {
    close_light();
    return false;
  }
  // A tap on a slider sets it to the nearest step.
  const int x0 = kSide + kKnob / 2;
  const int track_w = fb_.width() - 2 * kSide - kKnob;
  const int step = std::clamp((x - x0) * kLightSteps * 2 / track_w + 1, 0, 2 * kLightSteps) / 2;
  (item == 0 ? settings_.light : settings_.warmth) = static_cast<uint8_t>(step);
  light_sel_ = item;
  apply_light();
  return true;
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
  if (!reader_.open(*doc, static_cast<uint32_t>(index) + 1, reading_style(settings_, &book_fonts()), state.position,
                    fb_))
    return;
  library_.touch(index);
  reading_ = index;
  state.position = reader_.position();
  state.percent = static_cast<uint8_t>(reader_.percent());
  save_progress();
  show(Screen::Reader);
}

void App::show(Screen s) {
  if (s != Screen::Reader) word_mode_ = WordMode::Off;
  // Reading time runs in the book and its contents, and stops elsewhere.
  if (s == Screen::Reader && reading_ >= 0) {
    stats_.start(library_.entry(reading_).path, steady_ms());
  } else if (s != Screen::Contents) {
    stop_reading();
  }
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
    // The middle of the footer: the page number unless Settings asks for more.
    char middle[64] = "";
    char chapter[24], book[24];
    if (settings_.footer == FooterInfo::Percent) {
      std::snprintf(middle, sizeof(middle), "%d%%", reader_.percent());
    } else if (settings_.footer == FooterInfo::ChapterTime && time_left(chapter, book, sizeof(chapter))) {
      std::snprintf(middle, sizeof(middle), text().footer_left, chapter);
    }
    TextMark marks[48];
    const int n = page_marks(marks, 48);
    reader_.draw(fb_, text(), time, battery, page_bookmark() >= 0, middle, {marks, static_cast<size_t>(n)});
    if (word_mode_ != WordMode::Off) draw_word_overlay();
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
    case Screen::Transfer:
      draw_transfer();
      break;
    case Screen::Contents:
      draw_contents();
      break;
    case Screen::Battery:
      draw_battery();
      break;
    case Screen::Light:
      draw_light();
      break;
    case Screen::Check:
      draw_check();
      break;
    case Screen::Updates:
      draw_updates();
      break;
    case Screen::Send:
      draw_send();
      break;
    case Screen::Stats:
      draw_stats();
      break;
    case Screen::Reader:
      break;
  }
}

void App::start() {
  if (board_.power.woke_by_timer() || update_state_.night_restart) return night_update();
  if (battery_below(kEmptyPercent + 1)) return sleep(true);
  apply_light();
  record_battery(BatterySample::Kind::Wake);
  save_battery();
  // After sleep (a restart on the device), carry on reading where we were.
  if (resume_reader_ && library_.count() > 0) {
    resume_reader_ = false;
    open_book(library_.current());
    if (screen_ == Screen::Reader) return;
  }
  draw();
  queue_.submit_full();
}

// ---- Sleep -------------------------------------------------------------------

uint32_t App::wait_ms() const {
  constexpr uint32_t kForever = 60 * 60 * 1000;
  if (asleep_) return kForever;
  if (power_held_) return kHoldMs;
  if (touch_.holding()) return TouchTracker::kLongPressMs;
  if (peers_ticking()) return peer::kTickMs;
  const uint32_t idle = sleep_after_ms(settings_.sleep_after);
  if (idle == 0 || screen_ == Screen::Transfer) return kForever;  // books may still be arriving
  return idle;
}

void App::timeout() {
  if (asleep_) return;
  if (power_held_) {
    power_held_ = false;  // the release that follows does nothing
    sleep();
    return;
  }
  if (touch_.holding()) {
    // No events while the finger rests (the simulators): it is a long press.
    if (handle_gesture(touch_.hold())) {
      draw();
      queue_.submit();
    }
    return;
  }
  if (peers_ticking()) {
    if (service_peers(true)) {
      draw();
      queue_.submit();
    }
    return;
  }
  if (sleep_after_ms(settings_.sleep_after) != 0 && screen_ != Screen::Transfer) sleep();
}

bool App::battery_below(int percent) {
  const int level = board_.power.battery_percent();
  return level >= 0 && level < percent && !board_.power.charging();
}

void App::sleep(bool empty) {
  if (screen_ == Screen::Transfer) close_transfer();  // radio off first
  word_mode_ = WordMode::Off;
  if (screen_ == Screen::Send) {
    close_send();
    screen_ = Screen::Reader;  // wake to the book
  }
  // In the book, or about to reopen it (the battery ran out at start).
  const bool in_book = screen_ == Screen::Reader || screen_ == Screen::Contents || resume_reader_;
  if (in_book) save_progress();
  stop_reading();
  if (board_.storage != nullptr) {
    const char* state = in_book ? "reader\n" : "home\n";
    board_.storage->write(kStatePath, state, static_cast<uint32_t>(std::strlen(state)));
  }
  record_battery(BatterySample::Kind::Sleep);
  save_battery();
  if (empty) {
    board_.power.set_wake_timer(0);  // no radio at night on an empty battery
  } else {
    schedule_night_check();
  }
  before_sleep_ = screen_;
  asleep_ = true;
  draw_sleep(empty);
  queue_.submit_full();
  if (board_.frontlight != nullptr) board_.frontlight->set(0, 0);
  board_.power.sleep();  // a deep-sleeping board restarts from here on waking
}

void App::wake() {
  asleep_ = false;
  if (battery_below(kEmptyPercent + 1)) return sleep(true);
  apply_light();
  record_battery(BatterySample::Kind::Wake);
  show(before_sleep_);
}

void App::draw_sleep(bool empty) {
  hit_count_ = 0;
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  fb_.clear(Color::White);
  // The book being read, as a large cover: the screen keeps it while asleep.
  if (library_.count() > 0) {
    const LibraryEntry& book = library_.entry(library_.current());
    const int cw = w - 2 * kSleepCoverSide;
    const int ch = cw * 3 / 2;
    ui::draw_cover(fb_, {kSleepCoverSide, kSleepCoverTop, cw, ch}, book.title, book.author,
                   cover_of(library_.current(), true));
  }
  if (empty) {
    ui::draw_centered(fb_, ui::text_font(), h - 96, t.battery_empty);
    ui::draw_hint(fb_, t.charge_hint);
  } else {
    ui::draw_centered(fb_, ui::label_font(), h - 96, t.sleeping);
    ui::draw_hint(fb_, t.wake_hint);
  }
}

// ---- Device check ------------------------------------------------------------

void App::open_check() {
  check_ = {};
  show(Screen::Check);
}

// Returns true when the screen needs a redraw.
bool App::check_event(const InputEvent& ev) {
  const uint8_t bit = static_cast<uint8_t>(1u << static_cast<int>(ev.key));
  switch (ev.type) {
    case InputEvent::Type::KeyDown:
      if (ev.key == Key::Home && (check_.pressed & bit) != 0) {
        show(Screen::Settings);  // the second Home press leaves
        return false;
      }
      if (ev.key == Key::Power) power_held_ = true;  // holding it still sleeps
      check_.pressed |= bit;
      check_.down |= bit;
      return true;
    case InputEvent::Type::KeyUp:
      if (ev.key == Key::Power) {
        if (!power_held_) return false;  // it slept and woke
        power_held_ = false;
        check_.down &= static_cast<uint8_t>(~bit);
        draw();
        queue_.submit_full();  // Power also tests the full refresh
        return false;
      }
      check_.down &= static_cast<uint8_t>(~bit);
      return true;
    case InputEvent::Type::TouchDown:
    case InputEvent::Type::TouchMove: {
      int x, y;
      fb_.to_logical(ev.x, ev.y, x, y);
      check_.touched = check_.finger = true;
      check_.x = static_cast<int16_t>(x);
      check_.y = static_cast<int16_t>(y);
      check_.px = ev.x;
      check_.py = ev.y;
      return true;
    }
    case InputEvent::Type::TouchUp:
      check_.finger = false;
      return true;
    default:
      return false;
  }
}

void App::draw_check() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.device_check);

  // Marks in the four corners: all four visible means nothing is cut off.
  for (int i = 0; i < kCorner; ++i) {
    const int len = kCorner - i;
    fb_.hline(0, i, len, Color::Black);
    fb_.hline(w - len, i, len, Color::Black);
    fb_.hline(0, h - 1 - i, len, Color::Black);
    fb_.hline(w - len, h - 1 - i, len, Color::Black);
  }

  // One box per button: black while held, ticked once it has worked.
  ui::draw_label(fb_, kSide, kKeysLabelBase, t.buttons_label);
  constexpr int kKeys = 4;
  constexpr int kGap = 8;
  const int key_w = (w - 2 * kSide - (kKeys - 1) * kGap) / kKeys;
  for (int k = 0; k < kKeys; ++k) {
    const Rect r{kSide + k * (key_w + kGap), kKeysTop, key_w, kKeyHeight};
    const bool down = (check_.down >> k) & 1;
    const bool done = (check_.pressed >> k) & 1;
    Color fg = Color::Black;
    if (down) {
      fb_.fill_round_rect(r, 8, Color::Black);
      fg = Color::White;
    } else {
      fb_.fill_round_rect(r, 8, Color::Black);
      fb_.fill_round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, 6, Color::White);
    }
    const char* name = t.key_names[k];
    const int name_w = text_width(ui::label_font(), name) / 64;
    draw_text(fb_, ui::label_font(), (r.x + (r.w - name_w) / 2) * 64, r.y + 24, name, fg);
    if (done) {
      // A tick, three pixels thick (each line is two).
      const int cx = r.x + r.w / 2;
      const int cy = r.y + 40;
      for (int d = 0; d < 2; ++d) {
        draw_line(fb_, cx - 9, cy + d, cx - 3, cy + 6 + d, false, fg);
        draw_line(fb_, cx - 3, cy + 6 + d, cx + 9, cy - 6 + d, false, fg);
      }
    }
  }

  // Touch: the last point, where the finger was and as the panel saw it.
  ui::draw_label(fb_, kSide, kTouchLabelBase, t.touch_label);
  const Rect box{kSide, kTouchTop, w - 2 * kSide, kTouchHeight};
  fb_.draw_rect(box, Color::Black);
  if (!check_.touched) {
    ui::draw_wrapped(fb_, ui::text_font(), box.x + 16, box.y + 50, box.w - 32, 2, t.touch_here, false);
  } else {
    char where[64];
    std::snprintf(where, sizeof(where), "x %d  y %d", check_.x, check_.y);
    draw_text(fb_, ui::text_font(), (box.x + 16) * 64, box.y + 34, where);
    std::snprintf(where, sizeof(where), "(%d, %d)", check_.px, check_.py);
    draw_text(fb_, ui::label_font(), (box.x + 16) * 64, box.y + 60, where);
  }
  if (check_.touched) {
    // Crosshair where the finger is (or was), across the whole screen.
    const int x = check_.x;
    const int y = check_.y;
    for (int i = -24; i <= 24; ++i) {
      if (i > -5 && i < 5) continue;
      for (int d = 0; d < 2; ++d) {
        if (x + i >= 0 && x + i < w && y + d >= 0 && y + d < h) fb_.set_pixel(x + i, y + d, Color::Black);
        if (y + i >= 0 && y + i < h && x + d >= 0 && x + d < w) fb_.set_pixel(x + d, y + i, Color::Black);
      }
    }
    if (check_.finger) fb_.fill_round_rect({x - 4, y - 4, 10, 10}, 5, Color::Black);
  }

  // What the board says about itself.
  ui::draw_label(fb_, kSide, kAboutLabelBase, t.about_label);
  char screen[32], battery[48], clock[32], card[32];
  std::snprintf(screen, sizeof(screen), "%d \xC3\x97 %d", board_.display.width(), board_.display.height());  // ×
  const int level = board_.power.battery_percent();
  if (level >= 0) {
    std::snprintf(battery, sizeof(battery), "%d%% \xC2\xB7 %s", level,
                  board_.power.charging() ? t.charging : t.on_battery);
  } else {
    std::snprintf(battery, sizeof(battery), "%s", t.missing);
  }
  DateTime now;
  if (board_.clock != nullptr && board_.clock->now(now)) {
    std::snprintf(clock, sizeof(clock), "%04u-%02u-%02u %02u:%02u", now.year, now.month, now.day, now.hour, now.minute);
  } else {
    std::snprintf(clock, sizeof(clock), "%s", board_.clock == nullptr ? t.missing : t.not_set);
  }
  if (board_.storage != nullptr) {
    int books = 0;
    for (int i = 0; i < library_.count(); ++i) books += library_.entry(i).builtin == nullptr ? 1 : 0;
    format_book_count(settings_.language, books, card, sizeof(card));
  } else {
    std::snprintf(card, sizeof(card), "%s", t.missing);
  }
  // Free memory, when the board tells: its own RAM, then external RAM.
  char memory[40] = "";
  if (board_.memory != nullptr) {
    MemoryInfo m;
    board_.memory(m);
    const unsigned tenths = static_cast<unsigned>((uint64_t{m.external_free} * 10 + (1u << 19)) >> 20);
    std::snprintf(memory, sizeof(memory), "%u KB \xC2\xB7 %u.%u MB", static_cast<unsigned>(m.internal_free >> 10),
                  tenths / 10, tenths % 10);
  }
  const char* names[] = {t.device,      t.version,     t.screen, t.battery,    t.clock,
                         t.memory_card, t.front_light, t.wifi,   t.free_memory};
  const char* values[] = {board_.name,
                          board_.version,
                          screen,
                          battery,
                          clock,
                          card,
                          board_.frontlight != nullptr ? t.present : t.missing,
                          board_.transfer != nullptr ? t.present : t.missing,
                          memory};
  const int rows = board_.memory != nullptr ? 9 : 8;
  for (int i = 0; i < rows; ++i) {
    const int y = kAboutLabelBase + 10 + i * kAboutRowHeight;
    if (i > 0) dotted_rule(fb_, y);
    draw_text(fb_, ui::label_font(), kSide * 64, y + 25, names[i]);
    ui::draw_right(fb_, ui::label_font(), w - kSide, y + 25, values[i]);
  }

  ui::draw_hint(fb_, t.hint_check);
}

// ---- Input -------------------------------------------------------------------

bool App::handle(const InputEvent& ev) {
  if (!asleep_ && ev.type != InputEvent::Type::Quit) {
    sample_battery();
    if (battery_below(kEmptyPercent + 1)) {
      sleep(true);
      return true;
    }
  }
  if (asleep_) {
    if (ev.type == InputEvent::Type::Quit) return false;
    if (ev.type == InputEvent::Type::KeyDown) wake();  // its release is ignored
    return true;
  }
  if (screen_ == Screen::Check && ev.type != InputEvent::Type::Quit) {
    if (check_event(ev)) {
      draw();
      queue_.submit();
    }
    return true;
  }
  switch (ev.type) {
    case InputEvent::Type::Quit:
      return false;
    case InputEvent::Type::KeyDown:
      // Power acts when released, so holding it can mean sleep instead.
      if (ev.key == Key::Power) {
        power_held_ = true;
        return true;
      }
      if (!handle_key(ev.key)) return true;
      break;
    case InputEvent::Type::KeyUp:
      if (ev.key != Key::Power || !power_held_) return true;
      power_held_ = false;
      if (!handle_key(Key::Power)) return true;
      break;
    case InputEvent::Type::TouchDown:
    case InputEvent::Type::TouchMove:
    case InputEvent::Type::TouchUp:
      if (!handle_gesture(touch_.feed(ev, fb_))) return true;
      break;
    case InputEvent::Type::Transfer:
      if (screen_ != Screen::Transfer) return true;
      if (transfer_state_ == TransferState::Running) {
        TransferStatus st;
        board_.transfer->status(st);
        if (st.update == TransferStatus::Update::Installed) {
          finish_update();
          break;
        }
      }
      break;
    case InputEvent::Type::Peer:
      if (!service_peers(false)) return true;
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
  const uint32_t now = steady_ms();
  stats_.turn(now, today(board_.clock), step > 0);
  if (stats_.save_due(now) && board_.storage != nullptr) stats_.save(*board_.storage);
  return true;
}

// Returns true when the screen needs a redraw; screen changes draw themselves.
bool App::handle_key(Key key) {
  if (key == Key::Home) {
    if (screen_ == Screen::Home) {
      queue_.submit_full();  // already home: just clean the screen
    } else {
      if (screen_ == Screen::Reader && word_mode_ != WordMode::Off) return close_words(), true;
      if (screen_ == Screen::Reader || screen_ == Screen::Contents) save_progress();
      if (screen_ == Screen::Light && board_.storage != nullptr) save_settings(*board_.storage, settings_);
      if (screen_ == Screen::Transfer) return close_transfer(), false;
      if (screen_ == Screen::Settings && settings_reading_) {
        open_settings(false);  // back to the general page, on the way in
        settings_sel_ = kGeneralCount - 1;
        draw();
        queue_.submit();
        return false;
      }
      if (screen_ == Screen::Updates && update_busy_ != UpdateBusy::No) return false;
      if (screen_ == Screen::Send) close_send();
      show(Screen::Home);
    }
    return false;
  }

  const int step = key == Key::Next ? 1 : key == Key::Prev ? -1 : 0;
  switch (screen_) {
    case Screen::Reader:
      if (word_mode_ != WordMode::Off) return word_key(key);
      if (key == Key::Power) return open_contents(), false;
      return turn(step);
    case Screen::Contents: {
      if (key == Key::Power) return contents_activate(), false;
      const int count = contents_row_count();
      do {
        contents_sel_ = wrap_index(contents_sel_ + step, count);
      } while (step != 0 && contents_skipped(contents_sel_));
      return true;
    }
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
      settings_sel_ = wrap_index(settings_sel_ + step, settings_reading_ ? kReadingCount : kGeneralCount);
      return true;
    case Screen::Battery:
    case Screen::Stats:
      if (key == Key::Power) show(Screen::Settings);
      return false;
    case Screen::Light: {
      if (key == Key::Power) {
        if (light_sel_ == 2) return close_light(), false;
        ++light_sel_;
        return true;
      }
      if (light_sel_ == 2) {
        light_sel_ = step > 0 ? 0 : 1;  // back to a slider
        return true;
      }
      uint8_t& v = light_sel_ == 0 ? settings_.light : settings_.warmth;
      v = static_cast<uint8_t>(std::clamp(v + step, 0, kLightSteps));
      apply_light();
      return true;
    }
    case Screen::Check:
      return false;  // check_event() takes its keys
    case Screen::Updates:
      if (update_busy_ != UpdateBusy::No) return false;
      if (key == Key::Power) return updates_activate(), true;
      updates_sel_ = wrap_index(updates_sel_ + step, kUpdateRows);
      return true;
    case Screen::Send:
      if (key == Key::Power) return send_activate();
      if (sender_.state() == PeerSender::State::Looking && sender_.reader_count() > 0) {
        send_sel_ = wrap_index(send_sel_ + step, sender_.reader_count());
        return true;
      }
      return false;
    case Screen::Transfer:
      break;  // only Home leaves it
  }
  return false;
}

bool App::handle_gesture(const Gesture& g) {
  using K = Gesture::Kind;
  if (g.kind == K::None) return false;
  if (g.kind == K::LongPress && screen_ != Screen::Reader) {
    Gesture tap = g;  // holding a row or button is the same as tapping it
    tap.kind = K::Tap;
    return handle_gesture(tap);
  }

  if (screen_ == Screen::Reader) {
    if (word_mode_ != WordMode::Off) {
      if (g.kind == K::Tap) return word_tap(g.x, g.y);
      if (g.kind == K::LongPress) return word_hold(g.x, g.y);
      return false;  // the page stays put while words are chosen
    }
    switch (g.kind) {
      case K::LongPress:
        return word_hold(g.x, g.y);
      case K::Tap:
        if (g.x >= fb_.width() / 3 && g.x < fb_.width() * 2 / 3) return open_contents(), false;
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

  if (screen_ == Screen::Contents && g.kind != K::Tap) {
    // Swipes page through the rows, landing on the first one that opens.
    const int per_page = contents_rows_per_page();
    const int count = contents_row_count();
    const int pages = (count + per_page - 1) / per_page;
    if (pages <= 1) return false;
    const int forward = g.kind == K::SwipeLeft || g.kind == K::SwipeUp ? 1 : -1;
    contents_sel_ = wrap_index(contents_sel_ / per_page + forward, pages) * per_page;
    while (contents_sel_ + 1 < count && contents_skipped(contents_sel_)) ++contents_sel_;
    return true;
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
  if (screen_ == Screen::Battery || screen_ == Screen::Stats)
    return show(Screen::Settings), false;  // a tap anywhere goes back
  if (screen_ == Screen::Light) return light_tap(g.x, g.y);
  if (screen_ == Screen::Send) {
    // A reader's row sends to it; after sending, a tap anywhere goes on.
    const int item = hit_at(g.x, g.y);
    if (sender_.state() == PeerSender::State::Looking) {
      if (item < 0) return false;
      send_sel_ = item;
    }
    return send_activate();
  }

  const int item = hit_at(g.x, g.y);
  if (item < 0) return false;
  switch (screen_) {
    case Screen::Home:
      if (item == kUpdateNoticeItem) return open_updates(), false;
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
    case Screen::Contents:
      contents_sel_ = item;
      contents_activate();
      return false;
    case Screen::Updates:
      if (update_busy_ != UpdateBusy::No) return false;
      updates_sel_ = item;
      updates_activate();
      return true;
    case Screen::Reader:
    case Screen::Transfer:
    case Screen::Battery:
    case Screen::Light:
    case Screen::Check:
    case Screen::Send:
    case Screen::Stats:
      break;
  }
  return false;
}

void run(Board& board) {
  static App app(board);  // too large for a small task stack
  app.start();
  for (;;) {
    InputEvent ev;
    if (!board.input.wait(ev, app.wait_ms())) {
      app.timeout();
      continue;
    }
    if (!app.handle(ev)) return;
  }
}

}  // namespace vellum

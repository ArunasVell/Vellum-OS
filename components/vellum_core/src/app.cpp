// Screens and navigation: home, library, settings and the reader.
#include "vellum/app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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
constexpr int kSectionGap = 38;  // uppercase section label above a group of rows
constexpr int kSettingRowHeight = 40;

// Sleep screen geometry.
constexpr int kSleepCoverTop = 120;
constexpr int kSleepCoverSide = 90;

constexpr uint32_t kHoldMs = 1000;  // holding Power this long sleeps
constexpr const char* kStatePath = "vellum/state.txt";

// Contents geometry.
constexpr int kContentsRowHeight = 50;
constexpr int kContentsBottom = 60;  // room for the hint
constexpr int kIndent = 20;          // per level of a nested contents entry

// "Add books" geometry.
constexpr int kTransferTop = 192;  // baseline of the first step label
constexpr int kQrSize = 156;

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
enum class Row : uint8_t {
  Language,
  Clock,
  SleepAfter,
  TextSize,
  LineSpacing,
  WordSpacing,
  Margins,
  Alignment,
  Hyphenation
};
constexpr Row kRows[] = {Row::Language,    Row::Clock,   Row::SleepAfter, Row::TextSize,   Row::LineSpacing,
                         Row::WordSpacing, Row::Margins, Row::Alignment,  Row::Hyphenation};
constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);
constexpr int kGeneralRows = 3;  // rows before the reading section

int value_count(Row r) {
  switch (r) {
    case Row::TextSize:
    case Row::SleepAfter:
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
  if (board_.storage != nullptr) {
    load_settings(*board_.storage, settings_);
    char state[16];
    const int32_t n = textfile::read(*board_.storage, kStatePath, state, sizeof(state) - 1);
    resume_reader_ = n >= 6 && std::strncmp(state, "reader", 6) == 0;
  }
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
  return 1 + std::min(kHomeRows, library_.count() - 1) + 3;  // current book, others, three links
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
  if (home_sel_ == count - 1) return show(Screen::Settings);
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

// ---- Adding books over Wi-Fi -------------------------------------------------

void App::open_transfer() {
  if (board_.storage == nullptr) {
    transfer_state_ = TransferState::NoCard;
  } else if (board_.transfer == nullptr) {
    transfer_state_ = TransferState::Failed;
  } else {
    // Starting the radio takes a moment: say so first.
    transfer_state_ = TransferState::Off;
    show(Screen::Transfer);
    transfer_info_ = {};
    transfer_state_ = board_.transfer->start(transfer_info_) ? TransferState::Running : TransferState::Failed;
    draw();
    queue_.submit();
    return;
  }
  show(Screen::Transfer);
}

void App::close_transfer() {
  bool received = false;
  if (transfer_state_ == TransferState::Running) {
    TransferStatus st;
    board_.transfer->status(st);
    received = st.received > 0;
    board_.transfer->stop();
  }
  transfer_state_ = TransferState::Off;
  if (received) {
    reload_library();  // shows the home screen with the new books
  } else {
    show(Screen::Home);
  }
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
  draw_text(fb_, fonts::literata_italic_40, kSide * 64, kTitleBase, t.add_books);
  draw_text(fb_, fonts::literata_16, kSide * 64, kSubtitleBase, t.transfer_lead);

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
    ui::draw_wrapped(fb_, fonts::literata_20, kSide, kTransferTop + 40, w - 2 * kSide, 3, message, false);
    ui::draw_hint(fb_, t.hint_transfer);
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
  draw_text(fb_, fonts::literata_16, col_x * 64, y + 46, t.network);
  ui::draw_fitted(fb_, fonts::literata_26, col_x, y + 80, col_w, info.network);
  draw_text(fb_, fonts::literata_16, col_x * 64, y + 120, t.password);
  ui::draw_fitted(fb_, fonts::literata_26, col_x, y + 154, col_w, info.password);

  // Step 2: open the upload page.
  y += kQrSize + 46;
  ui::draw_label(fb_, kSide, y, t.step_open);
  ui::draw_qr(fb_, kSide, y + 12, kQrSize, info.address);
  const char* address = info.address;
  if (std::strncmp(address, "http://", 7) == 0) address += 7;  // what people type
  ui::draw_fitted(fb_, fonts::literata_26, col_x, y + 80, col_w, address);
  ui::draw_wrapped(fb_, fonts::literata_16, col_x, y + 118, col_w, 3, t.scan_hint, false);

  // What has arrived so far.
  TransferStatus st;
  board_.transfer->status(st);
  y += kQrSize + 46;
  ui::draw_label(fb_, kSide, y, t.received_label);
  if (st.received > 0) {
    char count[32];
    format_book_count(settings_.language, st.received, count, sizeof(count));
    ui::draw_right(fb_, fonts::literata_16, w - kSide, y, count);
  }
  char line[160];
  if (st.receiving[0] != '\0') {
    std::snprintf(line, sizeof(line), t.receiving, st.receiving);
  } else if (st.card_full) {
    std::snprintf(line, sizeof(line), "%s", t.card_full);
  } else if (st.received > 0) {
    std::snprintf(line, sizeof(line), "%s", st.last);
  } else {
    std::snprintf(line, sizeof(line), "%s", t.waiting);
  }
  ui::draw_fitted(fb_, fonts::literata_20, kSide, y + 34, w - 2 * kSide, line);

  ui::draw_hint(fb_, t.hint_transfer);
}

// ---- Contents and bookmarks ---------------------------------------------------

void App::open_contents() {
  contents_mark_count_ = library_.bookmarks_of(reading_, contents_marks_, Library::kMaxBookmarks);
  contents_sel_ = 0;
  show(Screen::Contents);
}

int App::contents_rows_per_page() const { return (fb_.height() - kListTop - kContentsBottom) / kContentsRowHeight; }

int App::contents_row_count() const {
  const Document& doc = *reader_.document();
  const int chapters = doc.toc_count() > 0 ? doc.toc_count() : doc.chapter_count();
  return 2 + (contents_mark_count_ > 0 ? 1 + contents_mark_count_ : 0) + 1 + chapters;
}

App::ContentsRow App::contents_row(int i, int& index) const {
  index = 0;
  if (i == 0) return ContentsRow::Back;
  if (i == 1) return ContentsRow::Mark;
  i -= 2;
  if (contents_mark_count_ > 0) {
    if (i == 0) return ContentsRow::BookmarksLabel;
    if (i <= contents_mark_count_) {
      index = i - 1;
      return ContentsRow::Bookmark;
    }
    i -= 1 + contents_mark_count_;
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
  const ReadingStyle style = reading_style(settings_);
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
    case ContentsRow::Bookmark:
      return jump_to(library_.bookmark(contents_marks_[index]).position);
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
    case ContentsRow::ChaptersLabel:
      return;
  }
}

void App::draw_contents() {
  const int w = fb_.width();
  const Strings& t = text();
  const Document& doc = *reader_.document();
  draw_text(fb_, fonts::literata_italic_40, kSide * 64, kTitleBase, t.contents);

  const int per_page = contents_rows_per_page();
  const int count = contents_row_count();
  const int pages = (count + per_page - 1) / per_page;
  const int page = contents_sel_ / per_page;
  int title_w = w - 2 * kSide;
  if (pages > 1) {
    char label[48];
    std::snprintf(label, sizeof(label), t.page_of, page + 1, pages);
    ui::draw_right(fb_, fonts::literata_16, w - kSide, kSubtitleBase, label);
    title_w -= text_width_px(fonts::literata_16, label) + 16;
  }
  ui::draw_fitted(fb_, fonts::literata_16, kSide, kSubtitleBase, title_w, library_.entry(reading_).title);

  const int mark = page_bookmark();
  const int current = current_chapter_row();
  const int first = page * per_page;
  const int last = std::min(count, first + per_page);
  ContentsRow prev_kind = ContentsRow::Back;
  for (int i = first; i < last; ++i) {
    int index;
    const ContentsRow kind = contents_row(i, index);
    const int y = kListTop + (i - first) * kContentsRowHeight;
    const Rect r = row_rect(fb_, y, kContentsRowHeight);
    const int base = y + 33;
    const bool selected = i == contents_sel_;
    const bool listed = kind == ContentsRow::Bookmark || kind == ContentsRow::Chapter;
    if (listed && i > first && prev_kind == kind && !selected && contents_sel_ != i - 1) dotted_rule(fb_, y);
    prev_kind = kind;
    switch (kind) {
      case ContentsRow::BookmarksLabel:
      case ContentsRow::ChaptersLabel:
        ui::draw_label(fb_, kSide, base + 6,
                       kind == ContentsRow::BookmarksLabel ? t.bookmarks_label : t.chapters_label);
        continue;
      case ContentsRow::Back:
      case ContentsRow::Mark:
        draw_link(fb_, {r.x, r.y + 2, r.w, kLinkHeight},
                  kind == ContentsRow::Back ? t.back_to_book
                  : mark >= 0               ? t.remove_bookmark
                                            : t.add_bookmark,
                  selected);
        break;
      case ContentsRow::Bookmark: {
        const Bookmark& b = library_.bookmark(contents_marks_[index]);
        const Color fg = highlight(fb_, r, selected);
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", b.percent);
        const int right = r.x + r.w - kPad;
        ui::draw_right(fb_, fonts::literata_16, right, base, percent, fg);
        const int x = r.x + kPad;
        ui::draw_fitted(fb_, fonts::literata_italic_20, x, base, right - 56 - x, b.snippet, fg);
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
        ui::draw_fitted(fb_, fonts::literata_20, x, base, right - 24 - x, title, fg);
        if (index == current) fb_.fill_round_rect({right - 8, base - 12, 8, 8}, 4, fg);  // you are here
        break;
      }
    }
    add_hit(r, i);
  }
  ui::draw_hint(fb_, t.hint_list);
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
    reader_.draw(fb_, text(), time, battery, page_bookmark() >= 0);
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
    case Screen::Reader:
      break;
  }
}

void App::start() {
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
  if (sleep_after_ms(settings_.sleep_after) != 0 && screen_ != Screen::Transfer) sleep();
}

void App::sleep() {
  if (screen_ == Screen::Transfer) close_transfer();  // radio off first
  const bool in_book = screen_ == Screen::Reader || screen_ == Screen::Contents;
  if (in_book) save_progress();
  if (board_.storage != nullptr) {
    const char* state = in_book ? "reader\n" : "home\n";
    board_.storage->write(kStatePath, state, static_cast<uint32_t>(std::strlen(state)));
  }
  before_sleep_ = screen_;
  asleep_ = true;
  draw_sleep();
  queue_.submit_full();
  board_.power.sleep();  // a deep-sleeping board restarts from here on waking
}

void App::wake() {
  asleep_ = false;
  show(before_sleep_);
}

void App::draw_sleep() {
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
    ui::draw_cover(fb_, {kSleepCoverSide, kSleepCoverTop, cw, ch}, book.title, book.author);
  }
  ui::draw_centered(fb_, fonts::literata_16, h - 96, t.sleeping);
  ui::draw_hint(fb_, t.wake_hint);
}

// ---- Input -------------------------------------------------------------------

bool App::handle(const InputEvent& ev) {
  if (asleep_) {
    if (ev.type == InputEvent::Type::Quit) return false;
    if (ev.type == InputEvent::Type::KeyDown) wake();  // its release is ignored
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
      if (screen_ == Screen::Reader || screen_ == Screen::Contents) save_progress();
      if (screen_ == Screen::Transfer) return close_transfer(), false;
      show(Screen::Home);
    }
    return false;
  }

  const int step = key == Key::Next ? 1 : key == Key::Prev ? -1 : 0;
  switch (screen_) {
    case Screen::Reader:
      if (key == Key::Power) return open_contents(), false;
      return turn(step);
    case Screen::Contents: {
      if (key == Key::Power) return contents_activate(), false;
      const int count = contents_row_count();
      int index;
      do {
        contents_sel_ = wrap_index(contents_sel_ + step, count);
      } while (step != 0 && (contents_row(contents_sel_, index) == ContentsRow::BookmarksLabel ||
                             contents_row(contents_sel_, index) == ContentsRow::ChaptersLabel));
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
      settings_sel_ = wrap_index(settings_sel_ + step, kRowCount);
      return true;
    case Screen::Transfer:
      break;  // only Home leaves it
  }
  return false;
}

bool App::handle_gesture(const Gesture& g) {
  using K = Gesture::Kind;
  if (g.kind == K::None) return false;

  if (screen_ == Screen::Reader) {
    switch (g.kind) {
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
    int index;
    while (contents_sel_ + 1 < count && (contents_row(contents_sel_, index) == ContentsRow::BookmarksLabel ||
                                         contents_row(contents_sel_, index) == ContentsRow::ChaptersLabel)) {
      ++contents_sel_;
    }
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
    case Screen::Contents:
      contents_sel_ = item;
      contents_activate();
      return false;
    case Screen::Reader:
    case Screen::Transfer:
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

// Words on the page: holding one looks it up in the card's dictionaries and
// offers to highlight it, or a passage from it; without touch, Contents ›
// Look up a word picks a line and then a word with the side buttons.
//
// The page stays as it is underneath. A panel takes the half of the screen
// away from the word in hand: its definition, scrolled with the side
// buttons, over a Highlight button (Power) and Close (Home).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/app.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

constexpr int kPanelH = 340;   // definition panel
constexpr int kBarH = 128;     // choosing where a passage ends
constexpr int kHintBarH = 60;  // choosing a line or word
constexpr int kButtonH = 44;
constexpr int kLineStep = 27;  // definition lines
constexpr int kMaxLines = 64;  // lines on a page
constexpr int kMaxDefLines = 256;

// Hit items for the overlay's buttons.
constexpr int kHitPrimary = 2000;  // Highlight, Remove highlight or Save
constexpr int kHitClose = 2001;    // Close or Cancel

int wrap(int i, int count) { return count <= 0 ? 0 : (i % count + count) % count; }

// Copies s into out, cut short at a whole UTF-8 character.
void copy_whole(const char* s, char* out, size_t size) {
  size_t n = std::min(std::strlen(s), size - 1);
  while (n > 0 && s[n] != '\0' && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
  std::memcpy(out, s, n);
  out[n] = '\0';
}

// The boxes of the word holding box i: a hyphenated word has two.
int span_first(const PageWords& w, int i) {
  while (i > 0 && w.words[i - 1].hyphen) --i;
  return i;
}
int span_last(const PageWords& w, int i) {
  while (i + 1 < w.count && w.words[i].hyphen) ++i;
  return i;
}

// The first word of each line on the page; returns how many lines.
int line_starts(const PageWords& w, int* out, int max) {
  int n = 0;
  for (int i = 0; i < w.count && n < max; ++i) {
    if (i == 0 || w.words[i].baseline != w.words[i - 1].baseline) out[n++] = i;
  }
  return n;
}

// A mark over words first..last (box indices) of the page.
TextMark mark_words(const PageWords& w, int first, int last, TextMark::Style style) {
  const TextPos end{w.words[last].begin.paragraph, w.words[last].begin.offset + 1};
  return {w.words[first].begin, end, style};
}

struct Line {
  uint16_t begin;
  uint16_t end;
};

// Breaks `s` into lines of at most max_w px at spaces and line breaks;
// a word longer than a line is split where it overflows.
int wrap_lines(const Font& font, std::string_view s, int max_w, Line* out, int max) {
  int n = 0;
  size_t i = 0;
  while (i < s.size() && n < max) {
    size_t para_end = s.find('\n', i);
    if (para_end == std::string_view::npos) para_end = s.size();
    size_t start = i;
    while (start < para_end && n < max) {
      size_t fit = start;
      size_t at = start;
      while (at < para_end) {
        size_t word_end = s.find(' ', at);
        if (word_end == std::string_view::npos || word_end > para_end) word_end = para_end;
        if (text_width_px(font, s.substr(start, word_end - start)) > max_w) break;
        fit = word_end;
        at = word_end + 1;
      }
      if (fit == start) {
        // One long word: as many characters as fit, at least one.
        size_t k = start;
        while (k < para_end) {
          size_t next = k;
          decode_utf8(s, next);
          if (fit > start && text_width_px(font, s.substr(start, next - start)) > max_w) break;
          fit = next;
          k = next;
        }
      }
      out[n++] = {static_cast<uint16_t>(start), static_cast<uint16_t>(fit)};
      start = fit;
      while (start < para_end && s[start] == ' ') ++start;
    }
    i = para_end + 1;
  }
  return n;
}

// A pill-shaped button with its top-left corner at (x, y); returns its area.
Rect draw_button(Framebuffer& fb, int x, int y, std::string_view label, bool filled) {
  const int w = text_width_px(ui::text_font(), label) + 40;
  const Rect r{x, y, w, kButtonH};
  fb.fill_round_rect(r, kButtonH / 2, Color::Black);
  if (!filled) fb.fill_round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, kButtonH / 2 - 2, Color::White);
  draw_text(fb, ui::text_font(), (x + 20) * 64, y + 29, label, filled ? Color::White : Color::Black);
  return r;
}

}  // namespace

int App::highlight_at_word(int i) const {
  const PageWords& w = reader_.words();
  if (reading_ < 0 || i < 0 || i >= w.count) return -1;
  const TextPos at = w.words[i].begin;
  for (int k = 0; k < highlights_.count(); ++k) {
    const Highlight& h = highlights_.at(k);
    if (h.begin.chapter != reader_.chapter() || std::strcmp(h.path, library_.entry(reading_).path) != 0) continue;
    if (!(at < h.begin.text) && at < h.end) return k;
  }
  return -1;
}

int App::page_marks(TextMark* out, int max) const {
  int n = 0;
  if (reading_ >= 0 && max > 0) {
    int found[32];
    const int count = highlights_.of_book(library_.entry(reading_).path, reader_.chapter(), found, 32);
    for (int k = 0; k < count && n < max - 2; ++k) {
      const Highlight& h = highlights_.at(found[k]);
      out[n++] = {h.begin.text, h.end, TextMark::Style::Underline};
    }
  }
  if (word_mode_ == WordMode::Off) return n;
  const PageWords& w = reader_.words();
  if (w.count == 0 || n >= max) return n;
  switch (word_mode_) {
    case WordMode::PickLine: {
      int starts[kMaxLines];
      const int lines = line_starts(w, starts, kMaxLines);
      if (word_line_ >= lines) break;
      const int last = word_line_ + 1 < lines ? starts[word_line_ + 1] - 1 : w.count - 1;
      out[n++] = mark_words(w, starts[word_line_], last, TextMark::Style::Boxed);
      break;
    }
    case WordMode::PickWord:
    case WordMode::Look:
      out[n++] = mark_words(w, span_first(w, word_sel_), span_last(w, word_sel_), TextMark::Style::Selected);
      break;
    case WordMode::Extend: {
      const int a = span_first(w, std::min(word_sel_, word_end_));
      const int b = span_last(w, std::max(word_sel_, word_end_));
      out[n++] = mark_words(w, a, b, TextMark::Style::Dotted);
      if (n < max) {
        out[n++] = mark_words(w, span_first(w, word_end_), span_last(w, word_end_), TextMark::Style::Selected);
      }
      break;
    }
    case WordMode::Off:
      break;
  }
  return n;
}

void App::open_word_pick() {
  fb_.set_rotation(rotation_for(Screen::Reader));  // the page's words as the reader lays them out
  word_mode_ = reader_.words().count > 0 ? WordMode::PickLine : WordMode::Off;
  word_line_ = 0;
  show(Screen::Reader);
}

void App::close_words() { word_mode_ = WordMode::Off; }

bool App::word_hold(int x, int y) {
  const int i = reader_.words().at(x, y);
  if (i < 0) return false;
  select_word(i);
  return true;
}

void App::select_word(int i) {
  const PageWords& w = reader_.words();
  if (i < 0 || i >= w.count) return;
  word_sel_ = span_first(w, i);
  word_mark_ = highlight_at_word(word_sel_);
  word_mode_ = WordMode::Look;
  look_up_word();
}

void App::look_up_word() {
  definition_scroll_ = 0;
  definition_lines_ = 0;
  lookup_result_ = {};
  if (definition_ != nullptr) definition_[0] = '\0';
  char word[96];
  reader_.word_text(word_sel_, word, sizeof(word));
  // The word as it is printed, without its punctuation, until a dictionary
  // gives its own spelling.
  char forms[1][dict::kFormBytes];
  if (dict::word_forms(word, "", forms, 1) == 1) {
    std::snprintf(lookup_result_.headword, sizeof(lookup_result_.headword), "%s", forms[0]);
  }
  if (!dictionaries_scanned_) {
    dictionaries_.scan(board_.storage);
    dictionaries_scanned_ = true;
  }
  if (dictionaries_.count() == 0 || definition_ == nullptr) {
    lookup_ = Lookup::NoDictionary;
    return;
  }
  const bool found = dictionaries_.lookup(
      word, reader_.document()->language(), text().preview_language, lookup_result_, definition_, kDefinitionBytes,
      [](void* ctx) {
        // The first lookup in a dictionary indexes it: say so meanwhile.
        auto* app = static_cast<App*>(ctx);
        app->lookup_ = Lookup::Preparing;
        app->draw();
        app->queue_.submit();
      },
      this);
  lookup_ = found ? Lookup::Found : Lookup::NotFound;
}

void App::save_highlight() {
  const PageWords& w = reader_.words();
  if (reading_ < 0 || w.count == 0) return close_words();
  const int a = span_first(w, std::min(word_sel_, word_end_));
  const int b = span_last(w, std::max(word_sel_, word_end_));
  Highlight h;
  const LibraryEntry& book = library_.entry(reading_);
  std::snprintf(h.path, sizeof(h.path), "%s", book.path);
  copy_whole(book.title, h.title, sizeof(h.title));
  h.begin = {static_cast<uint16_t>(reader_.chapter()), w.words[a].begin};
  h.end = {w.words[b].begin.paragraph, w.words[b].end};
  h.percent = static_cast<uint8_t>(reader_.percent());
  reader_.passage(h.begin.text, h.end, h.text, sizeof(h.text));
  DateTime now;
  if (board_.clock != nullptr && board_.clock->now(now)) {
    char date[40];
    std::snprintf(date, sizeof(date), "%04u-%02u-%02u %02u:%02u", unsigned{now.year}, unsigned{now.month},
                  unsigned{now.day}, unsigned{now.hour}, unsigned{now.minute});
    copy_whole(date, h.date, sizeof(h.date));
  }
  // A new passage takes the place of those it overlaps.
  for (int k = highlights_.count() - 1; k >= 0; --k) {
    const Highlight& old = highlights_.at(k);
    if (old.begin.chapter != h.begin.chapter || std::strcmp(old.path, h.path) != 0) continue;
    if (old.begin.text < h.end && h.begin.text < old.end) highlights_.remove(k);
  }
  highlights_.add(h);
  if (board_.storage != nullptr) highlights_.save(*board_.storage);
  close_words();
}

bool App::word_key(Key key) {
  const PageWords& w = reader_.words();
  if (w.count == 0) return close_words(), true;
  const int step = key == Key::Next ? 1 : key == Key::Prev ? -1 : 0;
  // The next or previous word, a hyphenated one counting once.
  auto move = [&](int i) {
    if (step > 0) return span_last(w, i) + 1 < w.count ? span_last(w, i) + 1 : 0;
    if (step < 0) return span_first(w, span_first(w, i) > 0 ? span_first(w, i) - 1 : w.count - 1);
    return i;
  };
  switch (word_mode_) {
    case WordMode::PickLine: {
      int starts[kMaxLines];
      const int lines = line_starts(w, starts, kMaxLines);
      if (key == Key::Power) {
        word_sel_ = starts[std::min(word_line_, lines - 1)];
        word_mode_ = WordMode::PickWord;
      } else {
        word_line_ = wrap(word_line_ + step, lines);
      }
      return true;
    }
    case WordMode::PickWord:
      if (key == Key::Power) {
        select_word(word_sel_);
      } else {
        word_sel_ = move(word_sel_);
      }
      return true;
    case WordMode::Look:
      if (key == Key::Power) {
        if (word_mark_ >= 0) {
          highlights_.remove(word_mark_);
          if (board_.storage != nullptr) highlights_.save(*board_.storage);
          close_words();
        } else {
          word_mode_ = WordMode::Extend;
          word_end_ = word_sel_;
        }
        return true;
      }
      // A long definition scrolls a panel at a time, keeping one line.
      if (definition_lines_ <= definition_rows_) return false;
      definition_scroll_ = std::clamp(definition_scroll_ + step * std::max(1, definition_rows_ - 1), 0,
                                      definition_lines_ - definition_rows_);
      return true;
    case WordMode::Extend:
      if (key == Key::Power) {
        save_highlight();
      } else {
        word_end_ = move(word_end_);
      }
      return true;
    case WordMode::Off:
      break;
  }
  return false;
}

bool App::word_tap(int x, int y) {
  const int item = hit_at(x, y);
  const Rect& r = word_overlay_;
  const bool on_overlay = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
  const int word = on_overlay ? -1 : reader_.words().at(x, y);
  switch (word_mode_) {
    case WordMode::Look:
      if (item == kHitPrimary) return word_key(Key::Power);
      if (item == kHitClose || (!on_overlay && word < 0)) return close_words(), true;
      if (word >= 0) return select_word(word), true;
      return false;
    case WordMode::Extend:
      if (item == kHitPrimary) return save_highlight(), true;
      if (item == kHitClose) return close_words(), true;
      if (word < 0) return false;
      word_end_ = word;  // the passage ends here
      save_highlight();
      return true;
    case WordMode::PickLine:
    case WordMode::PickWord:
      if (word >= 0) return select_word(word), true;
      return close_words(), true;
    case WordMode::Off:
      break;
  }
  return false;
}

void App::draw_word_overlay() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  const PageWords& words = reader_.words();
  // The overlay sits away from the word or line in hand.
  int focus = word_mode_ == WordMode::Extend ? word_end_ : word_sel_;
  if (word_mode_ == WordMode::PickLine) {
    int starts[kMaxLines];
    const int lines = line_starts(words, starts, kMaxLines);
    focus = lines > 0 ? starts[std::min(word_line_, lines - 1)] : 0;
  }
  const int focus_y = words.count > 0 ? words.words[std::clamp(focus, 0, words.count - 1)].baseline : 0;
  int height = word_mode_ == WordMode::Look ? kPanelH : word_mode_ == WordMode::Extend ? kBarH : kHintBarH;
  bool at_top = focus_y > h - height - 40;
  // The definition stays off the word's line: between the bottom of the
  // line before it and the top of the line after it.
  int line_above = 0;
  int line_below = h;
  for (int i = 0; i < words.count; ++i) {
    const WordBox& b = words.words[i];
    if (b.baseline < focus_y) line_above = std::max(line_above, b.y + b.h + 3);
    if (b.baseline > focus_y) line_below = std::min(line_below, b.y - 3);
  }
  if (word_mode_ == WordMode::Look && height > (at_top ? line_above : h - line_below)) {
    // A short (landscape) page: the side of the word with more room.
    at_top = line_above > h - line_below;
    height = std::max(at_top ? line_above : h - line_below, kBarH);
  }
  // Grown to the edge of the nearest line, so no line shows cut in half.
  int top_edge = at_top ? 0 : h - height;
  int bottom_edge = at_top ? height : h;
  for (int i = 0; i < words.count; ++i) {
    const WordBox& b = words.words[i];
    if (!at_top && b.y < top_edge && b.y + b.h + 4 > top_edge) {
      // Just below the line before it, clear of its descenders.
      int above = 0;
      for (int k = 0; k < i; ++k) {
        if (words.words[k].baseline < b.baseline) above = std::max(above, words.words[k].y + words.words[k].h);
      }
      top_edge = std::min<int>(b.y, above + 3);
      break;
    }
    if (at_top && b.y < bottom_edge && b.y + b.h + 4 > bottom_edge) {
      int below = h;
      for (int k = i + 1; k < words.count; ++k) {
        if (words.words[k].baseline > b.baseline) below = std::min<int>(below, words.words[k].y);
      }
      bottom_edge = std::max(b.y + b.h, below - 3);
    }
  }
  if (word_mode_ == WordMode::Look) {
    top_edge = at_top ? top_edge : std::max(top_edge, line_below);
    bottom_edge = at_top ? std::min(bottom_edge, line_above) : bottom_edge;
  }
  const Rect area{0, top_edge, w, bottom_edge - top_edge};
  word_overlay_ = area;
  fb_.fill_rect(area, Color::White);
  fb_.fill_rect({0, at_top ? area.y + area.h - 2 : area.y, w, 2}, Color::Black);  // edge toward the page
  const int top = area.y + (at_top ? 0 : 2);
  const int bottom = area.y + area.h - (at_top ? 2 : 0);

  if (word_mode_ == WordMode::PickLine || word_mode_ == WordMode::PickWord) {
    ui::draw_centered(fb_, ui::label_font(), (top + bottom) / 2 + 6,
                      word_mode_ == WordMode::PickLine ? t.hint_pick_line : t.hint_pick_word);
    return;
  }

  const int button_y = bottom - kButtonH - 16;
  if (word_mode_ == WordMode::Extend) {
    ui::draw_wrapped(fb_, ui::label_font(), kSide, top + 30, w - 2 * kSide, 2, t.hint_extend, false);
    const Rect save = draw_button(fb_, kSide, button_y, t.save, true);
    add_hit(save, kHitPrimary);
    add_hit(draw_button(fb_, save.x + save.w + 12, button_y, t.cancel, false), kHitClose);
    return;
  }

  // The definition: headword, dictionary, text, then the buttons.
  const int head_base = top + 44;
  int head_w = w - 2 * kSide;
  if (lookup_ == Lookup::Found) {
    ui::draw_right(fb_, ui::label_font(), w - kSide, head_base, lookup_result_.dictionary);
    head_w -= std::min(text_width_px(ui::label_font(), lookup_result_.dictionary), head_w / 2) + 16;
  }
  ui::draw_fitted(fb_, ui::italic_font(), kSide, head_base, head_w, lookup_result_.headword);

  const int text_top = head_base + 34;
  definition_rows_ = std::max(1, (button_y - 14 - text_top) / kLineStep + 1);
  const char* message = nullptr;
  switch (lookup_) {
    case Lookup::NoDictionary:
      message = t.no_dictionary;
      break;
    case Lookup::NotFound:
      message = t.not_in_dictionary;
      break;
    case Lookup::Preparing:
      message = t.preparing_dictionary;
      break;
    case Lookup::Found:
      break;
  }
  if (message != nullptr) {
    definition_lines_ = 0;
    ui::draw_wrapped(fb_, ui::text_font(), kSide, text_top, w - 2 * kSide, definition_rows_, message, false);
  } else {
    static Line lines[kMaxDefLines];
    const std::string_view def(definition_);
    definition_lines_ = wrap_lines(ui::text_font(), def, w - 2 * kSide, lines, kMaxDefLines);
    definition_scroll_ = std::clamp(definition_scroll_, 0, std::max(0, definition_lines_ - definition_rows_));
    for (int k = 0; k < definition_rows_ && definition_scroll_ + k < definition_lines_; ++k) {
      const Line& line = lines[definition_scroll_ + k];
      draw_text(fb_, ui::text_font(), kSide * 64, text_top + k * kLineStep,
                def.substr(line.begin, line.end - line.begin));
    }
    if (definition_lines_ > definition_rows_) {
      // Where in a long definition: "2/3", by panels.
      const int step = std::max(1, definition_rows_ - 1);
      const int pages = (definition_lines_ - definition_rows_ + step - 1) / step + 1;
      const int page = (definition_scroll_ + step - 1) / step + 1;
      char where[32];
      std::snprintf(where, sizeof(where), "%d/%d", page, pages);
      ui::draw_right(fb_, ui::label_font(), w - kSide, button_y + 28, where);
    }
  }
  const Rect primary = draw_button(fb_, kSide, button_y, word_mark_ >= 0 ? t.remove_highlight : t.highlight, true);
  add_hit(primary, kHitPrimary);
  add_hit(draw_button(fb_, primary.x + primary.w + 12, button_y, t.close, false), kHitClose);
}

}  // namespace vellum

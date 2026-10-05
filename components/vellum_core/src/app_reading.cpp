// Reading extras: links and footnotes, Go to, and the question at the end
// of a book. They are panels over the page like those of app_words.cpp
// (word_mode_), so Home closes them and the page stays underneath.
//
// Tapping a link follows it; without touch, Contents › Links on this page
// picks one with the side buttons. A footnote shows over the page instead,
// in the definition panel. After a link or Go to, Contents › Back to where
// you were returns.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/app.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

constexpr int kGoPanelH = 300;      // Go to
constexpr int kFinishPanelH = 180;  // finished the book?
constexpr int kButtonH = 44;
constexpr int kKnob = 18;
constexpr int kHitNoteClose = 2001;  // the definition panel's Close (app_words.cpp)

// Hit items for the panels' buttons.
constexpr int kHitGo = 3000;
constexpr int kHitCancel = 3001;
constexpr int kHitLess = 3002;
constexpr int kHitMore = 3003;
constexpr int kHitTrack = 3004;
constexpr int kHitRead = 3005;
constexpr int kHitNotYet = 3006;

int wrap(int i, int count) { return count <= 0 ? 0 : (i % count + count) % count; }

bool in_link(const Link& l, TextPos p) { return !(p < l.begin) && p < l.end; }

// A pill-shaped button with its top-left corner at (x, y); returns its area.
Rect draw_button(Framebuffer& fb, int x, int y, std::string_view label, bool filled) {
  const int w = std::max(kButtonH + 8, text_width_px(ui::text_font(), label) + 40);
  const Rect r{x, y, w, kButtonH};
  fb.fill_round_rect(r, kButtonH / 2, Color::Black);
  if (!filled) fb.fill_round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, kButtonH / 2 - 2, Color::White);
  const int text_x = x + (w - text_width_px(ui::text_font(), label)) / 2;
  draw_text(fb, ui::text_font(), text_x * 64, y + 29, label, filled ? Color::White : Color::Black);
  return r;
}

}  // namespace

int App::link_at_word(int i) const {
  const PageWords& w = reader_.words();
  if (i < 0 || i >= w.count) return -1;
  const ChapterMarkup m = reader_.document()->markup();
  for (uint32_t k = 0; k < m.link_count; ++k) {
    if (in_link(m.links[k], w.words[i].begin)) return static_cast<int>(k);
  }
  return -1;
}

int App::page_links(int16_t* out, int max) const {
  const ChapterMarkup m = reader_.document()->markup();
  if (m.link_count == 0) return 0;
  const PageWords& w = reader_.words();
  if (w.count == 0) return 0;
  const TextPos first = w.words[0].begin;
  const TextPos last = w.words[w.count - 1].begin;
  int n = 0;
  for (uint32_t k = 0; k < m.link_count && n < max; ++k) {
    const Link& l = m.links[k];
    if (last < l.begin || !(first < l.end)) continue;
    for (int i = 0; i < w.count; ++i) {
      if (in_link(l, w.words[i].begin)) {
        out[n++] = static_cast<int16_t>(k);
        break;
      }
    }
  }
  return n;
}

void App::focus_link(int i) {
  const PageWords& w = reader_.words();
  const ChapterMarkup m = reader_.document()->markup();
  word_sel_ = 0;
  if (i < 0 || static_cast<uint32_t>(i) >= m.link_count) return;
  for (int k = 0; k < w.count; ++k) {
    if (in_link(m.links[i], w.words[k].begin)) {
      word_sel_ = k;
      return;
    }
  }
}

bool App::link_tap(int x, int y, bool& redraw) {
  if (reader_.document()->markup().link_count == 0) return false;
  const int link = link_at_word(reader_.words().at(x, y, 8));
  if (link < 0) return false;
  redraw = follow_link(link);
  return true;
}

bool App::follow_link(int i) {
  Document& doc = *reader_.document();
  const ChapterMarkup m = doc.markup();
  if (i < 0 || static_cast<uint32_t>(i) >= m.link_count) return close_words(), true;
  const Link link = m.links[i];
  LinkTarget to;
  const size_t room = definition_ != nullptr ? kDefinitionBytes : 0;
  if (!doc.follow(link, to, definition_, room)) return close_words(), true;
  if (to.note && room > 0) {
    // A footnote: shown over the page like a definition, the page kept.
    lookup_ = Lookup::Found;
    lookup_result_ = {};
    std::snprintf(lookup_result_.headword, sizeof(lookup_result_.headword), "%s", text().note);
    definition_scroll_ = 0;
    definition_lines_ = 0;
    link_sel_ = i;
    focus_link(i);
    word_mode_ = WordMode::Note;
    return true;
  }
  // Elsewhere in the book; Contents › Back to where you were returns.
  link_back_ = reader_.position();
  link_back_book_ = reading_;
  close_words();
  jump_to({to.chapter, {to.paragraph, 0}});
  return false;
}

void App::open_link_pick() {
  page_link_count_ = page_links(page_links_, static_cast<int>(sizeof(page_links_) / sizeof(page_links_[0])));
  if (page_link_count_ > 0) {
    word_mode_ = WordMode::PickLink;
    link_pick_ = 0;
    link_sel_ = page_links_[0];
    focus_link(link_sel_);
  }
  show(Screen::Reader);
}

void App::open_go_to() {
  go_percent_ = reader_.percent();
  go_fine_ = false;
  word_mode_ = WordMode::GoTo;
  show(Screen::Reader);
}

void App::go_to_percent() {
  link_back_ = reader_.position();
  link_back_book_ = reading_;
  close_words();
  if (reader_.go_to_percent(go_percent_)) {
    BookState& state = library_.state(reading_);
    state.position = reader_.position();
    state.percent = static_cast<uint8_t>(reader_.percent());
    save_progress();
  }
  show(Screen::Reader);
}

void App::open_finish() {
  finish_asked_ = reading_;
  word_mode_ = WordMode::Finish;
}

bool App::reading_key(Key key) {
  const int step = key == Key::Next ? 1 : key == Key::Prev ? -1 : 0;
  switch (word_mode_) {
    case WordMode::PickLink:
      if (key == Key::Power) return follow_link(link_sel_);
      link_pick_ = wrap(link_pick_ + step, page_link_count_);
      link_sel_ = page_links_[link_pick_];
      focus_link(link_sel_);
      return true;
    case WordMode::Note: {
      // A long note scrolls a panel at a time; past its ends, it closes.
      const int before = definition_scroll_;
      if (key != Key::Power && definition_lines_ > definition_rows_) {
        definition_scroll_ = std::clamp(definition_scroll_ + step * std::max(1, definition_rows_ - 1), 0,
                                        definition_lines_ - definition_rows_);
      }
      if (definition_scroll_ == before) close_words();
      return true;
    }
    case WordMode::GoTo:
      if (key == Key::Power) {
        if (go_fine_) return go_to_percent(), false;
        go_fine_ = true;
        return true;
      }
      go_percent_ = std::clamp(go_percent_ + step * (go_fine_ ? 1 : 10), 0, 100);
      return true;
    case WordMode::Finish:
      // Power marks it read; Next reads on (the question stays on the last
      // page); Prev and Home say not yet.
      if (key == Key::Power) {
        library_.state(reading_).finished = true;
        save_progress();
      }
      if (key == Key::Next && at_last_page()) return false;
      close_words();
      return key == Key::Next ? (turn(1), true) : true;
    default:
      break;
  }
  return false;
}

bool App::reading_tap(int x, int y) {
  const int item = hit_at(x, y);
  const Rect& r = word_overlay_;
  const bool on_panel = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
  switch (word_mode_) {
    case WordMode::PickLink: {
      const int link = on_panel ? -1 : link_at_word(reader_.words().at(x, y, 8));
      if (link >= 0) return follow_link(link);
      return close_words(), true;
    }
    case WordMode::Note:
      if (item == kHitNoteClose || !on_panel) close_words();
      return true;
    case WordMode::GoTo:
      if (item == kHitGo) return go_to_percent(), false;
      if (item == kHitCancel || !on_panel) return close_words(), true;
      if (item == kHitLess || item == kHitMore) {
        go_percent_ = std::clamp(go_percent_ + (item == kHitMore ? 1 : -1), 0, 100);
      } else if (item == kHitTrack) {
        const int track_w = fb_.width() - 2 * kSide;
        go_percent_ = std::clamp(((x - kSide) * 100 + track_w / 2) / track_w, 0, 100);
      }
      return true;
    case WordMode::Finish:
      if (item == kHitRead) return reading_key(Key::Power);
      if (item == kHitNotYet || !on_panel) return close_words(), true;
      return false;
    default:
      break;
  }
  return false;
}

void App::draw_reading_panel() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  const int height = word_mode_ == WordMode::GoTo ? kGoPanelH : kFinishPanelH;
  const Rect area{0, h - height, w, height};
  word_overlay_ = area;
  fb_.fill_rect(area, Color::White);
  fb_.fill_rect({0, area.y, w, 2}, Color::Black);  // edge toward the page

  if (word_mode_ == WordMode::Finish) {
    ui::draw_fitted(fb_, ui::italic_font(), kSide, area.y + 56, w - 2 * kSide, t.finished_question);
    const Rect read = draw_button(fb_, kSide, area.y + 86, t.mark_read, true);
    add_hit(read, kHitRead);
    add_hit(draw_button(fb_, read.x + read.w + 12, area.y + 86, t.not_yet, false), kHitNotYet);
    return;
  }

  // Go to: the percent, the chapter there, a track to tap, buttons, hint.
  char value[16];
  std::snprintf(value, sizeof(value), "%d%%", go_percent_);
  ui::draw_right(fb_, ui::large_font(), w - kSide, area.y + 50, value);
  ui::draw_fitted(fb_, ui::italic_font(), kSide, area.y + 50,
                  w - 2 * kSide - text_width_px(ui::large_font(), value) - 16, t.go_to);
  const Document& doc = *reader_.document();
  const int chapter = reader_.chapter_at(go_percent_);
  char numbered[32];
  std::snprintf(numbered, sizeof(numbered), t.chapter_n, chapter + 1);
  const char* where = numbered;
  int entry = -1;  // the first contents entry of the last chapter up to there
  for (int k = 0; k < doc.toc_count(); ++k) {
    const int c = doc.toc(k).chapter;
    if (c <= chapter && (entry < 0 || c > doc.toc(entry).chapter)) entry = k;
  }
  if (entry >= 0) where = doc.toc(entry).title;
  ui::draw_fitted(fb_, ui::label_font(), kSide, area.y + 84, w - 2 * kSide, where);

  const int track_y = area.y + 128;
  const int track_w = w - 2 * kSide;
  ui::draw_progress(fb_, kSide, track_y, track_w, go_percent_, 100);
  const int knob_x = kSide + track_w * go_percent_ / 100;
  fb_.fill_round_rect({knob_x - kKnob / 2, track_y - kKnob / 2, kKnob, kKnob}, kKnob / 2, Color::Black);
  add_hit({kSide - 12, track_y - 24, track_w + 24, 48}, kHitTrack);

  const int buttons_y = area.y + 176;
  const Rect less = draw_button(fb_, kSide, buttons_y, "\xE2\x80\x93", false);  // –
  add_hit(less, kHitLess);
  const Rect more = draw_button(fb_, less.x + less.w + 8, buttons_y, "+", false);
  add_hit(more, kHitMore);
  const Rect go = draw_button(fb_, more.x + more.w + 20, buttons_y, t.go, true);
  add_hit(go, kHitGo);
  add_hit(draw_button(fb_, go.x + go.w + 8, buttons_y, t.cancel, false), kHitCancel);
  ui::draw_centered(fb_, ui::label_font(), area.y + height - 26, go_fine_ ? t.hint_go_fine : t.hint_go_coarse);
}

}  // namespace vellum

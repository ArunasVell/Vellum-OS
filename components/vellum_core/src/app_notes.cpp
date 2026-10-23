// What's new: a version's release notes, grouped into new features, bug
// fixes and other changes, a page at a time.
//
// They show in two places. When Settings › Updates finds a new version, its
// notes (from the release server, kept on the card in vellum/notes.txt) come
// up before installing, with an Install button; the Install row opens them
// again later. And the first time a new version starts with a card, however
// it was installed (over Wi-Fi, from the phone page, over USB), it shows the
// notes built into it once (Board::release_notes), with Done.
//
// The side buttons (or a swipe, or a tap on the text) turn the pages, Power
// presses the button, Home goes back.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "vellum/app.h"
#include "vellum/text.h"
#include "vellum/textfile.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

// Geometry, logical pixels on the 480x800 portrait screen.
constexpr int kTitleBase = 112;
constexpr int kSubtitleBase = 142;
constexpr int kBodyTop = 168;  // top of the first entry
constexpr int kHeadingH = 40;  // a group's label, baseline 28 down
constexpr int kGroupGap = 14;  // above a group that is not first on the page
constexpr int kItemGap = 8;    // between changes
constexpr int kBullet = 24;    // the text's indent after its bullet
constexpr int kButtonHeight = 50;
constexpr int kHintRoom = 56;  // below the button
constexpr int kButtonItem = 0;
constexpr int kTextItem = 1;

// The group headings CI writes, in the order of Strings::note_groups.
constexpr const char* kGroups[3] = {"New features", "Bug fixes", "Other changes"};

// An outlined, rounded button with its label centred; inverted in hand.
void draw_button(Framebuffer& fb, Rect r, std::string_view label) {
  fb.fill_round_rect(r, r.h / 2, Color::Black);
  const int text_w = text_width_px(ui::text_font(), label);
  if (text_w <= r.w - 24) {
    draw_text(fb, ui::text_font(), (r.x + (r.w - text_w) / 2) * 64, r.y + 32, label, Color::White);
  } else {
    ui::draw_fitted(fb, ui::text_font(), r.x + 12, r.y + 32, r.w - 24, label, Color::White);
  }
}

}  // namespace

void App::keep_release_notes(const char* latest) {
  if (board_.updater == nullptr || notes_ == nullptr) return;
  board_.updater->notes(notes_, kNotesBytes);
  const size_t n = tidy_release_notes(notes_, std::strlen(notes_), notes_, kNotesBytes);
  textfile::copy(n > 0 ? latest : "", notes_version_, sizeof(notes_version_));
  // The card's copy belongs to UpdateState::latest, saved with it.
  if (board_.storage == nullptr) return;
  if (n > 0) {
    board_.storage->write(kReleaseNotesPath, notes_, static_cast<uint32_t>(n));
  } else {
    board_.storage->remove(kReleaseNotesPath);
  }
}

bool App::offered_notes() {
  if (notes_ == nullptr || !update_available()) return false;
  if (std::strcmp(notes_version_, update_state_.latest) == 0) return notes_[0] != '\0';
  // Found before a restart: from the card.
  if (board_.storage == nullptr || textfile::read(*board_.storage, kReleaseNotesPath, notes_, kNotesBytes) <= 0) {
    return false;
  }
  textfile::copy(update_state_.latest, notes_version_, sizeof(notes_version_));
  return true;
}

void App::open_notes(bool offer, Screen back, bool then_book) {
  notes_offer_ = offer;
  notes_back_ = back;
  notes_then_book_ = then_book;
  notes_page_ = 0;
  show(Screen::Notes);
}

bool App::show_new_version_notes() {
  if (board_.storage == nullptr || std::strcmp(update_state_.started, board_.version) == 0) return false;
  textfile::copy(board_.version, update_state_.started, sizeof(update_state_.started));
  save_update_state(*board_.storage, update_state_);
  if (notes_ == nullptr || board_.release_notes == nullptr) return false;
  if (tidy_release_notes(board_.release_notes, std::strlen(board_.release_notes), notes_, kNotesBytes) == 0)
    return false;
  textfile::copy(board_.version, notes_version_, sizeof(notes_version_));
  const bool book = resume_reader_ && library_.count() > 0;
  resume_reader_ = false;
  open_notes(false, Screen::Home, book);
  return true;
}

void App::close_notes() {
  if (notes_then_book_) {
    notes_then_book_ = false;
    open_book(library_.current());
    if (screen_ == Screen::Reader) return;
  }
  if (notes_back_ == Screen::Updates) return open_updates();
  show(notes_back_);
}

bool App::notes_key(Key key) {
  if (key == Key::Power) {
    if (!notes_offer_) return close_notes(), false;
    // Install from the Updates screen, which shows how far it got.
    open_updates();
    install_update();
    return true;
  }
  if (notes_pages_ <= 1) return false;
  const int step = key == Key::Prev ? -1 : 1;
  notes_page_ = (notes_page_ + step + notes_pages_) % notes_pages_;
  return true;
}

void App::draw_notes() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_back_title(t.whats_new, kTitleBase);
  char line[64];
  std::snprintf(line, sizeof(line), "Vellum %s", notes_version_);
  draw_text(fb_, ui::label_font(), kSide * 64, kSubtitleBase, line);

  // The button, and the hint below it.
  const Rect button{kSide, fb_.height() - kHintRoom - kButtonHeight, w - 2 * kSide, kButtonHeight};
  char label[64];
  if (notes_offer_) {
    std::snprintf(label, sizeof(label), t.install_version, notes_version_);
  } else {
    std::snprintf(label, sizeof(label), "%s", t.done);
  }
  draw_button(fb_, button, label);
  add_hit(button, kButtonItem);
  ui::draw_hint(fb_, notes_offer_ ? t.hint_notes : t.hint_notes_done);

  // The notes, laid out from the start each time: the pages are counted on
  // the way, and only the one in view is drawn.
  const Font& font = ui::text_font();
  const int bottom = button.y - 16;
  const int text_x = kSide + kBullet;
  const int text_w = w - kSide - text_x;
  int page = 0;
  int y = kBodyTop;
  bool first = true;  // on its page
  std::string_view rest = notes_ != nullptr ? std::string_view(notes_) : std::string_view{};
  while (!rest.empty()) {
    const size_t end = rest.find('\n');
    const std::string_view entry = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
    if (entry.empty()) continue;
    const bool heading = entry[0] == '#';
    const int lines = heading ? 0 : ui::wrapped_line_count(font, text_w, entry);
    // A heading keeps a line of its first change with it.
    const auto height = [&] {
      return heading ? (first ? 0 : kGroupGap) + kHeadingH : lines * font.line_height + kItemGap;
    };
    const int need = heading ? height() + font.line_height : font.line_height;
    if (!first && y + need > bottom) {
      ++page;
      y = kBodyTop;
      first = true;
    }
    const int h = height();
    if (page == notes_page_) {
      if (heading) {
        std::string_view name = entry.substr(1);
        for (int i = 0; i < 3; ++i) {
          if (name == kGroups[i]) name = t.note_groups[i];
        }
        ui::draw_label(fb_, kSide, y + h - kHeadingH + 28, name);
      } else {
        const int base = y + font.ascent;
        draw_text(fb_, font, kSide * 64, base, "\xE2\x80\xA2");  // •
        // A change too long for the page is cut short.
        const int room = std::max(1, (bottom - y) / font.line_height);
        ui::draw_wrapped(fb_, font, text_x, base, text_w, std::min(lines, room), entry, false);
      }
    }
    y += h;
    first = false;
  }
  notes_pages_ = page + 1;
  if (notes_page_ >= notes_pages_) notes_page_ = 0;
  add_hit({0, kBodyTop, w, bottom - kBodyTop}, kTextItem);

  if (notes_pages_ > 1) {
    std::snprintf(line, sizeof(line), "%d / %d", notes_page_ + 1, notes_pages_);
    ui::draw_right(fb_, ui::label_font(), w - kSide, kSubtitleBase, line);
  }
}

}  // namespace vellum

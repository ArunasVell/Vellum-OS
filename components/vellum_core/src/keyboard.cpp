#include "vellum/keyboard.h"

#include <algorithm>
#include <cstring>

#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {

// What a key does.
enum class KeyKind : uint8_t { Char, Shift, Back, Space, Symbols, Language, Ok, Cancel };

struct Keyboard::KeyDef {
  KeyKind kind;
  uint8_t row;    // 0..4; Cancel sits in the text line
  uint8_t width;  // in half keys: a letter is 2, a row is 20
  const char* lower;
  const char* upper;
};

namespace {

using KeyDef = Keyboard::KeyDef;
using K = KeyKind;

constexpr int kRowUnits = 20;  // half keys across the screen
constexpr int kMargin = 4;     // left and right of the keys
constexpr int kLineH = 64;     // the text line, under the top rule
constexpr int kRowPitch = 60;
constexpr int kKeyH = 52;
constexpr int kKeyGap = 2;  // each side of a key, inside its share of the row
constexpr int kRadius = 7;

constexpr KeyDef kCancelKey{K::Cancel, 255, 0, "", ""};

constexpr KeyDef kDigits[] = {
    {K::Char, 0, 2, "1", "1"}, {K::Char, 0, 2, "2", "2"}, {K::Char, 0, 2, "3", "3"}, {K::Char, 0, 2, "4", "4"},
    {K::Char, 0, 2, "5", "5"}, {K::Char, 0, 2, "6", "6"}, {K::Char, 0, 2, "7", "7"}, {K::Char, 0, 2, "8", "8"},
    {K::Char, 0, 2, "9", "9"}, {K::Char, 0, 2, "0", "0"},
};

constexpr KeyDef kLithuanian[] = {
    {K::Char, 0, 2, "\xC4\x85", "\xC4\x84"},  // ą Ą
    {K::Char, 0, 2, "\xC4\x8D", "\xC4\x8C"},  // č Č
    {K::Char, 0, 2, "\xC4\x99", "\xC4\x98"},  // ę Ę
    {K::Char, 0, 2, "\xC4\x97", "\xC4\x96"},  // ė Ė
    {K::Char, 0, 2, "\xC4\xAF", "\xC4\xAE"},  // į Į
    {K::Char, 0, 2, "\xC5\xA1", "\xC5\xA0"},  // š Š
    {K::Char, 0, 2, "\xC5\xB3", "\xC5\xB2"},  // ų Ų
    {K::Char, 0, 2, "\xC5\xAB", "\xC5\xAA"},  // ū Ū
    {K::Char, 0, 2, "\xC5\xBE", "\xC5\xBD"},  // ž Ž
};

// Rows 1 to 4 of both letter pages.
constexpr KeyDef kLetters[] = {
    {K::Char, 1, 2, "q", "Q"},          {K::Char, 1, 2, "w", "W"},
    {K::Char, 1, 2, "e", "E"},          {K::Char, 1, 2, "r", "R"},
    {K::Char, 1, 2, "t", "T"},          {K::Char, 1, 2, "y", "Y"},
    {K::Char, 1, 2, "u", "U"},          {K::Char, 1, 2, "i", "I"},
    {K::Char, 1, 2, "o", "O"},          {K::Char, 1, 2, "p", "P"},
    {K::Char, 2, 2, "a", "A"},          {K::Char, 2, 2, "s", "S"},
    {K::Char, 2, 2, "d", "D"},          {K::Char, 2, 2, "f", "F"},
    {K::Char, 2, 2, "g", "G"},          {K::Char, 2, 2, "h", "H"},
    {K::Char, 2, 2, "j", "J"},          {K::Char, 2, 2, "k", "K"},
    {K::Char, 2, 2, "l", "L"},          {K::Shift, 3, 3, "", ""},
    {K::Char, 3, 2, "z", "Z"},          {K::Char, 3, 2, "x", "X"},
    {K::Char, 3, 2, "c", "C"},          {K::Char, 3, 2, "v", "V"},
    {K::Char, 3, 2, "b", "B"},          {K::Char, 3, 2, "n", "N"},
    {K::Char, 3, 2, "m", "M"},          {K::Back, 3, 3, "", ""},
    {K::Symbols, 4, 3, "?123", "?123"}, {K::Language, 4, 3, "", ""},
    {K::Space, 4, 10, " ", " "},        {K::Ok, 4, 4, "", ""},
};

// Rows 1 to 4 of the symbols page.
constexpr KeyDef kSymbols[] = {
    {K::Char, 1, 2, "-", "-"},        {K::Char, 1, 2, "/", "/"},
    {K::Char, 1, 2, ":", ":"},        {K::Char, 1, 2, ";", ";"},
    {K::Char, 1, 2, "(", "("},        {K::Char, 1, 2, ")", ")"},
    {K::Char, 1, 2, "&", "&"},        {K::Char, 1, 2, "@", "@"},
    {K::Char, 1, 2, "\"", "\""},      {K::Char, 1, 2, "'", "'"},
    {K::Char, 2, 2, ".", "."},        {K::Char, 2, 2, ",", ","},
    {K::Char, 2, 2, "?", "?"},        {K::Char, 2, 2, "!", "!"},
    {K::Char, 2, 2, "#", "#"},        {K::Char, 2, 2, "%", "%"},
    {K::Char, 2, 2, "+", "+"},        {K::Char, 2, 2, "=", "="},
    {K::Char, 2, 2, "*", "*"},        {K::Char, 3, 2, "_", "_"},
    {K::Char, 3, 2, "$", "$"},        {K::Char, 3, 2, "\xE2\x82\xAC", "\xE2\x82\xAC"},  // €
    {K::Char, 3, 2, "~", "~"},        {K::Char, 3, 2, "<", "<"},
    {K::Char, 3, 2, ">", ">"},        {K::Char, 3, 2, "[", "["},
    {K::Char, 3, 2, "]", "]"},        {K::Back, 3, 4, "", ""},
    {K::Symbols, 4, 3, "ABC", "ABC"}, {K::Language, 4, 3, "", ""},
    {K::Space, 4, 10, " ", " "},      {K::Ok, 4, 4, "", ""},
};

constexpr int kDigitCount = sizeof(kDigits) / sizeof(kDigits[0]);
constexpr int kLithuanianCount = sizeof(kLithuanian) / sizeof(kLithuanian[0]);
constexpr int kLetterCount = sizeof(kLetters) / sizeof(kLetters[0]);
constexpr int kSymbolCount = sizeof(kSymbols) / sizeof(kSymbols[0]);

// Length of the UTF-8 character ending at text[n - 1].
int last_char_bytes(const char* text, int n) {
  int k = n - 1;
  while (k > 0 && (static_cast<uint8_t>(text[k]) & 0xC0) == 0x80) --k;
  return n - k;
}

// A solid arrow pointing up (shift), with its tip at (cx, top).
void draw_up_arrow(Framebuffer& fb, int cx, int top, int size, Color c) {
  const int head = size / 2;
  for (int dy = 0; dy < head; ++dy) fb.hline(cx - dy, top + dy, 2 * dy + 1, c);
  fb.fill_rect({cx - size / 5, top + head, 2 * (size / 5) + 1, size - head}, c);
}

// A key cap pointing left with a cross in it (backspace).
void draw_back_icon(Framebuffer& fb, int cx, int cy, Color fg, Color bg) {
  constexpr int kH = 20;
  constexpr int kBody = 22;
  const int left = cx - (kBody + kH / 2) / 2;
  for (int dx = 0; dx < kH / 2; ++dx) fb.vline(left + dx, cy - dx, 2 * dx + 1, fg);
  fb.fill_rect({left + kH / 2, cy - kH / 2, kBody, kH + 1}, fg);
  const int x0 = left + kH / 2 + kBody / 2 - 5;
  for (int i = 0; i <= 10; ++i) {
    fb.fill_rect({x0 + i, cy - 5 + i, 2, 1}, bg);
    fb.fill_rect({x0 + i, cy + 5 - i, 2, 1}, bg);
  }
}

// An open bracket lying down (space).
void draw_space_icon(Framebuffer& fb, int cx, int cy, Color c) {
  constexpr int kW = 40;
  fb.fill_rect({cx - kW / 2, cy + 4, kW, 2}, c);
  fb.fill_rect({cx - kW / 2, cy - 2, 2, 6}, c);
  fb.fill_rect({cx + kW / 2 - 2, cy - 2, 2, 6}, c);
}

}  // namespace

void Keyboard::open(std::string_view text, const Options& options) {
  options_ = options;
  options_.max_bytes = std::clamp(options_.max_bytes, 1, kMaxBytes);
  length_ = 0;
  text_[0] = '\0';
  type(text);
  page_ = options.page;
  shift_ = options_.capitalize && length_ == 0;
  open_ = true;
  show_cursor_ = false;
  cursor_ = find(page_ == Page::Symbols ? "-" : "q");
}

int Keyboard::key_count() const {
  if (page_ == Page::Symbols) return 1 + kDigitCount + kSymbolCount;
  return 1 + (page_ == Page::Lithuanian ? kLithuanianCount : kDigitCount) + kLetterCount;
}

const Keyboard::KeyDef& Keyboard::def(int i) const {
  if (i <= 0) return kCancelKey;
  --i;
  const KeyDef* top = page_ == Page::Lithuanian ? kLithuanian : kDigits;
  const int top_count = page_ == Page::Lithuanian ? kLithuanianCount : kDigitCount;
  if (i < top_count) return top[i];
  return (page_ == Page::Symbols ? kSymbols : kLetters)[i - top_count];
}

int Keyboard::row_of(int i) const { return i == 0 ? -1 : def(i).row; }

int Keyboard::find(std::string_view label) const {
  for (int i = 1; i < key_count(); ++i) {
    const KeyDef& d = def(i);
    if (d.kind == KeyKind::Char && label == d.lower) return i;
    if (d.kind == KeyKind::Symbols && label == d.lower) return i;
  }
  // Function keys by a name of their own.
  const KeyKind kind = label == "shift"      ? KeyKind::Shift
                       : label == "back"     ? KeyKind::Back
                       : label == "space"    ? KeyKind::Space
                       : label == "language" ? KeyKind::Language
                       : label == "symbols"  ? KeyKind::Symbols
                       : label == "ok"       ? KeyKind::Ok
                       : label == "cancel"   ? KeyKind::Cancel
                                             : KeyKind::Char;
  if (kind == KeyKind::Cancel) return options_.cancel.empty() ? -1 : 0;
  for (int i = 1; kind != KeyKind::Char && i < key_count(); ++i) {
    if (def(i).kind == kind) return i;
  }
  return -1;
}

Rect Keyboard::key_rect(const Framebuffer& fb, int i) const {
  const Rect a = area(fb);
  if (i == 0) {
    if (options_.cancel.empty()) return {};
    const int w = text_width_px(ui::text_font(), options_.cancel) + 28;
    return {a.x + a.w - ui::kSide + 12 - w, a.y + 12, w, 44};
  }
  // The row's span and this key's place in it.
  const int row = def(i).row;
  int first = i;
  while (first > 1 && def(first - 1).row == row) --first;
  int units = 0, before = 0;
  for (int k = first; k < key_count() && def(k).row == row; ++k) {
    if (k < i) before += def(k).width;
    units += def(k).width;
  }
  const int start = (kRowUnits - units) / 2 + before;  // short rows are centred
  const int span = a.w - 2 * kMargin;
  const int x0 = a.x + kMargin + start * span / kRowUnits;
  const int x1 = a.x + kMargin + (start + def(i).width) * span / kRowUnits;
  const int y = a.y + kLineH + 6 + row * kRowPitch;
  return {x0 + kKeyGap, y, x1 - x0 - 2 * kKeyGap, kKeyH};
}

bool Keyboard::type(std::string_view s) {
  if (length_ + static_cast<int>(s.size()) > options_.max_bytes) return false;
  std::memcpy(text_ + length_, s.data(), s.size());
  length_ += static_cast<int>(s.size());
  text_[length_] = '\0';
  return true;
}

void Keyboard::backspace() {
  if (length_ == 0) return;
  length_ -= last_char_bytes(text_, length_);
  text_[length_] = '\0';
}

void Keyboard::set_page(Page p) {
  // Keep the cursor in the same row, as near its place as the row allows.
  const int row = row_of(cursor_);
  int col = 0;
  while (cursor_ - col - 1 >= 1 && row_of(cursor_ - col - 1) == row) ++col;
  page_ = p;
  if (row < 0) return;
  int first = 1;
  while (first < key_count() && row_of(first) != row) ++first;
  int last = first;
  while (last + 1 < key_count() && row_of(last + 1) == row) ++last;
  cursor_ = std::min(first + col, last);
}

Keyboard::Result Keyboard::press(int i) {
  const KeyDef& d = def(i);
  switch (d.kind) {
    case KeyKind::Char: {
      if (!type(shift_ ? d.upper : d.lower)) return Result::None;
      shift_ = false;
      return Result::Typed;
    }
    case KeyKind::Space:
      return type(" ") ? Result::Typed : Result::None;
    case KeyKind::Back:
      if (length_ == 0) return Result::None;
      backspace();
      if (length_ == 0 && options_.capitalize) shift_ = true;
      return Result::Typed;
    case KeyKind::Shift:
      shift_ = !shift_;
      return Result::Redraw;
    case KeyKind::Symbols:
      set_page(page_ == Page::Symbols ? Page::English : Page::Symbols);
      return Result::Redraw;
    case KeyKind::Language:
      set_page(page_ == Page::English      ? Page::Lithuanian
               : page_ == Page::Lithuanian ? Page::English
                                           : Page::Lithuanian);
      return Result::Redraw;
    case KeyKind::Ok:
      return Result::Done;
    case KeyKind::Cancel:
      return Result::Cancel;
  }
  return Result::None;
}

Keyboard::Result Keyboard::key(Key k) {
  if (!open_ || k == Key::Home) return Result::None;
  if (!show_cursor_) {
    show_cursor_ = true;  // the first press shows where the cursor is
    return Result::Redraw;
  }
  if (k == Key::Power) return press(cursor_);
  const int first = options_.cancel.empty() ? 1 : 0;
  const int n = key_count() - first;
  cursor_ = first + ((cursor_ - first + (k == Key::Next ? 1 : -1)) % n + n) % n;
  return Result::Redraw;
}

Keyboard::Result Keyboard::tap(const Framebuffer& fb, int x, int y) {
  if (!open_) return Result::None;
  const Rect a = area(fb);
  if (y < a.y) return Result::None;
  show_cursor_ = false;  // fingers do not need it
  // The key under the finger, or the nearest in the row it is on.
  int best = -1;
  int best_d = 1 << 30;
  for (int i = options_.cancel.empty() ? 1 : 0; i < key_count(); ++i) {
    const Rect r = key_rect(fb, i);
    const int dx = x < r.x ? r.x - x : x >= r.x + r.w ? x - (r.x + r.w - 1) : 0;
    const int dy = y < r.y ? r.y - y : y >= r.y + r.h ? y - (r.y + r.h - 1) : 0;
    if (dy > (kRowPitch - kKeyH) / 2 + 2) continue;  // another row
    if (dx < best_d) {
      best_d = dx;
      best = i;
    }
  }
  if (best < 0 || best_d > 12) return Result::None;
  if (best != 0) cursor_ = best;
  return press(best);
}

void Keyboard::draw_line(Framebuffer& fb) const {
  const Rect a = area(fb);
  const Rect cancel = key_rect(fb, 0);
  const int left = ui::kSide;
  const int right = (options_.cancel.empty() ? a.x + a.w - ui::kSide : cancel.x - 14);
  const int base = a.y + 44;
  const Font& font = ui::large_font();
  // The end of the text when it is longer than the line; then the caret.
  std::string_view shown = text();
  const int room = right - left - 4;
  while (!shown.empty() && text_width_px(font, shown) > room) {
    size_t i = 0;
    decode_utf8(shown, i);
    shown.remove_prefix(i);
  }
  int caret = left;
  if (shown.empty()) {
    if (!options_.prompt.empty()) {
      ui::draw_fitted(fb, ui::small_italic_font(), left + 6, base - 2, room - 6, options_.prompt);
    }
  } else {
    caret = (draw_text(fb, font, left * 64, base, shown) + 63) / 64 + 1;
  }
  fb.fill_rect({caret, base - font.ascent + 2, 2, font.ascent + 4}, Color::Black);
  fb.hline(left, base + 10, right - left, Color::Black);

  if (!options_.cancel.empty()) {
    const bool selected = show_cursor_ && cursor_ == 0;
    const Color fg = selected ? Color::White : Color::Black;
    fb.fill_round_rect(cancel, kRadius, Color::Black);
    if (!selected)
      fb.fill_round_rect({cancel.x + 1, cancel.y + 1, cancel.w - 2, cancel.h - 2}, kRadius - 1, Color::White);
    draw_text(fb, ui::text_font(), (cancel.x + 14) * 64, cancel.y + 29, options_.cancel, fg);
  }
}

void Keyboard::draw_key(Framebuffer& fb, int i) const {
  const KeyDef& d = def(i);
  const Rect r = key_rect(fb, i);
  const bool selected = show_cursor_ && cursor_ == i;
  // OK is the one dark key; the cursor inverts the key under it.
  const bool dark = (d.kind == KeyKind::Ok) != selected;
  const Color fg = dark ? Color::White : Color::Black;
  const Color bg = dark ? Color::Black : Color::White;
  fb.fill_round_rect(r, kRadius, Color::Black);
  if (!dark) {
    const int t = d.kind == KeyKind::Char || d.kind == KeyKind::Space ? 1 : 2;  // function keys stand out
    fb.fill_round_rect({r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t}, kRadius - t, Color::White);
  } else if (selected && d.kind == KeyKind::Ok) {
    fb.fill_round_rect({r.x + 3, r.y + 3, r.w - 6, r.h - 6}, kRadius - 3, Color::White);  // OK under the cursor
  }
  const Color ink = selected && d.kind == KeyKind::Ok ? Color::Black : fg;
  const Color paper = selected && d.kind == KeyKind::Ok ? Color::White : bg;
  const int cx = r.x + r.w / 2;
  const int cy = r.y + r.h / 2;

  auto centred = [&](const Font& font, std::string_view s, int base) {
    draw_text(fb, font, (cx - text_width_px(font, s) / 2) * 64, base, s, ink);
  };
  switch (d.kind) {
    case KeyKind::Char:
      centred(ui::large_font(), shift_ ? d.upper : d.lower, r.y + 35);
      break;
    case KeyKind::Shift:
      draw_up_arrow(fb, cx, cy - 11, 22, ink);
      if (!shift_) draw_up_arrow(fb, cx, cy - 6, 12, paper);  // hollow while off
      if (!shift_) fb.fill_rect({cx - 2, cy, 5, 7}, paper);
      break;
    case KeyKind::Back:
      draw_back_icon(fb, cx, cy, ink, paper);
      break;
    case KeyKind::Space:
      draw_space_icon(fb, cx, cy, ink);
      break;
    case KeyKind::Symbols:
      centred(ui::label_font(), d.lower, r.y + 32);
      break;
    case KeyKind::Language:
      // The page it leads to.
      centred(ui::label_font(), page_ == Page::English || page_ == Page::Symbols ? "\xC4\x84\xC4\x8C" : "ABC",
              r.y + 32);  // ĄČ
      break;
    case KeyKind::Ok:
      centred(ui::text_font(), options_.ok, r.y + 33);
      break;
    case KeyKind::Cancel:
      break;
  }
}

void Keyboard::draw(Framebuffer& fb) const {
  if (!open_) return;
  const Rect a = area(fb);
  fb.fill_rect(a, Color::White);
  fb.fill_rect({a.x, a.y, a.w, 2}, Color::Black);  // edge toward the screen above
  draw_line(fb);
  for (int i = 1; i < key_count(); ++i) draw_key(fb, i);
}

}  // namespace vellum

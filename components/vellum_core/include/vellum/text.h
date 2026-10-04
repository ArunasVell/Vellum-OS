// Text measurement, drawing and page layout.
//
// Positions inside a line are kept in 1/64 pixel, so kerning and justified
// spacing do not accumulate rounding errors; glyphs snap to whole pixels
// only when drawn.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vellum/fonts.h"
#include "vellum/framebuffer.h"
#include "vellum/hyphenation.h"

namespace vellum {

// Decodes the UTF-8 sequence at s[i] and advances i past it. Malformed
// input yields U+FFFD and skips one byte.
uint32_t decode_utf8(std::string_view s, size_t& i);

// Width of `text` in 1/64 px, kerning included.
int32_t text_width(const Font& font, std::string_view text);

// Draws `text` starting at x (1/64 px) with its baseline at y. Returns the
// pen position after the last glyph, in 1/64 px.
int32_t draw_text(Framebuffer& fb, const Font& font, int32_t x, int y, std::string_view text, Color c = Color::Black);

// Width in whole pixels, for centring and right-aligning short strings.
inline int text_width_px(const Font& font, std::string_view text) { return (text_width(font, text) + 63) / 64; }

// A paragraph that starts with this byte is a heading: set centred, and the
// paragraph after it is not indented.
constexpr char kHeadingMark = '\x01';

// A position in a sequence of paragraphs: paragraph index and byte offset.
struct TextPos {
  uint32_t paragraph = 0;
  uint32_t offset = 0;
  bool operator==(const TextPos&) const = default;
};

// TextPos order: paragraph first, then offset.
inline bool operator<(TextPos a, TextPos b) {
  return a.paragraph < b.paragraph || (a.paragraph == b.paragraph && a.offset < b.offset);
}

// One word as it was set on a page: where it sits and which bytes it is. A
// word hyphenated at the end of a line is two boxes, the first with `hyphen`.
struct WordBox {
  int16_t x, y, w, h;  // logical pixels: pen start to end, ascent to descent
  int16_t baseline;
  bool hyphen;    // continues in the next box, on the next line
  int32_t pen;    // x in 1/64 px, for drawing the word again
  TextPos begin;  // its first byte
  uint32_t end;   // one past its last byte, in the same paragraph
};

// The words of one page, in reading order. A fixed array: filling it
// allocates nothing.
struct PageWords {
  static constexpr int kMax = 512;
  WordBox words[kMax];
  int count = 0;
  // The word at logical point (x, y), or the nearest on that line within
  // `slop` px; -1 when there is none.
  int at(int x, int y, int slop = 12) const;
};

struct PageStyle {
  const Font* font;
  Rect area;                     // where text may go, logical pixels
  int indent;                    // first-line indent of every paragraph but the first, px
  const Hyphenator* hyphenator;  // language rules for breaking words; nullptr = none
  int line_height = 0;           // baseline to baseline, px; 0 = the font's own
  int word_space = 100;          // narrowest space between words, percent of the font's space
  bool justify = true;           // false = ragged right
};

// Lays out one page of `paragraphs` from `start`: justified lines (when
// style.justify), the last line of each paragraph set ragged, words hyphenated when the next one does
// not fit. Draws when `fb` is not null, so the
// same call paginates (fb = nullptr) and renders. Returns where the next
// page starts; that equals {count, 0} once all text is placed. When `words`
// is not null it receives the page's word boxes (up to PageWords::kMax).
TextPos typeset_page(const PageStyle& style, const char* const* paragraphs, uint32_t count, TextPos start,
                     Framebuffer* fb, PageWords* words = nullptr);

}  // namespace vellum

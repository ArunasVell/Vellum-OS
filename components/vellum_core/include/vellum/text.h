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

// A position in a sequence of paragraphs: paragraph index and byte offset.
struct TextPos {
  uint32_t paragraph = 0;
  uint32_t offset = 0;
  bool operator==(const TextPos&) const = default;
};

struct PageStyle {
  const Font* font;
  Rect area;   // where text may go, logical pixels
  int indent;  // first-line indent of every paragraph but the first, px
};

// Lays out one page of `paragraphs` from `start`: justified lines, the last
// line of each paragraph set ragged. Draws when `fb` is not null, so the
// same call paginates (fb = nullptr) and renders. Returns where the next
// page starts; that equals {count, 0} once all text is placed.
TextPos typeset_page(const PageStyle& style, const char* const* paragraphs, uint32_t count, TextPos start,
                     Framebuffer* fb);

}  // namespace vellum

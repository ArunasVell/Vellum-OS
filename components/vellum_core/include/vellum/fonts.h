// Bitmap fonts, pre-rendered on the build machine by tools/fontconvert.py.
//
// Each glyph is stored as 2-bit coverage (0 = paper, 3 = full ink) so the
// same data can later drive grayscale anti-aliasing. Advances and kerning
// are in 1/64 pixel so text keeps sub-pixel spacing.
#pragma once

#include <cstdint>

namespace vellum {

struct Glyph {
  uint32_t codepoint;
  uint32_t offset;  // into Font::bitmap; rows are packed back to back, 4 pixels per byte
  uint8_t width;
  uint8_t height;
  int8_t left;  // from the pen position to the bitmap's left edge
  int8_t top;   // from the baseline up to the bitmap's top row
  uint16_t advance;
};

struct KernPair {
  uint16_t left;  // glyph indices
  uint16_t right;
  int16_t adjust;  // added to the left glyph's advance, 1/64 px
};

struct Font {
  const char* family;
  uint8_t size;  // pixels per em
  uint8_t ascent;
  uint8_t descent;
  uint8_t line_height;
  const Glyph* glyphs;  // sorted by codepoint
  uint16_t glyph_count;
  const uint8_t* bitmap;
  const KernPair* kerning;  // sorted by (left, right)
  uint16_t kerning_count;
  uint16_t fallback;  // glyph index used for missing characters

  // Glyph index for `cp`, or `fallback` when the font lacks it.
  uint16_t find(uint32_t cp) const;
  // Kerning between two glyph indices, in 1/64 px.
  int kern(uint16_t left, uint16_t right) const;
};

namespace fonts {
// Literata (SIL Open Font License), designed for reading on screens.
extern const Font literata_26;         // body text
extern const Font literata_italic_26;  // emphasis and headings
extern const Font literata_20;         // small print: headers and page numbers
extern const Font literata_italic_20;
}  // namespace fonts

}  // namespace vellum

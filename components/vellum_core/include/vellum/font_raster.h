// Fonts made on the device from TrueType or OpenType files (outlines in
// glyf or CFF), for typefaces that are not built in: the same character
// set and Font format as tools/fontconvert.py, rendered by stb_truetype.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/fonts.h"

namespace vellum {

// One font file, ready to render at any size. The data must stay valid
// while the face is used.
class TtfFace {
 public:
  TtfFace();
  ~TtfFace();
  TtfFace(const TtfFace&) = delete;
  TtfFace& operator=(const TtfFace&) = delete;

  // False when `data` is not a font stb_truetype can read.
  bool load(const uint8_t* data, size_t size);
  bool loaded() const { return loaded_; }
  // How big the face looks: the height of "x" and the average advance of
  // a to z, both as fractions of the em. False when it has no "x".
  bool proportions(float& x_height, float& width) const;

 private:
  friend class RasterFont;
  struct Pair {
    uint16_t left, right;  // indices into the character set
    int16_t units;         // kerning in font units
  };
  void find_kerning();

  struct Info;
  Info* info_;
  bool loaded_ = false;
  Pair* pairs_ = nullptr;  // found on first use, for every size
  int pair_count_ = -1;
};

// A Font rendered from a face at one size, owning its tables.
class RasterFont {
 public:
  RasterFont() = default;
  ~RasterFont() { clear(); }
  RasterFont(const RasterFont&) = delete;
  RasterFont& operator=(const RasterFont&) = delete;

  // Renders the character set at `px` pixels per em. `family` names it.
  bool build(TtfFace& face, int px, const char* family);
  void clear();
  bool built() const { return glyphs_ != nullptr; }
  const Font& font() const { return font_; }

 private:
  Glyph* glyphs_ = nullptr;
  uint8_t* bitmap_ = nullptr;
  KernPair* kerning_ = nullptr;
  char family_[48] = {};
  Font font_{};
};

}  // namespace vellum

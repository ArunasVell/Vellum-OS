#include "vellum/font_raster.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) ((void)0)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vellum {
namespace {

// The character set of the built-in fonts (tools/fontconvert.py): Latin-1,
// Latin Extended-A, Cyrillic for Ukrainian and typographic punctuation, in
// code point order.
constexpr uint32_t kExtra[] = {0x490,  0x491,  0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C,
                               0x201D, 0x201E, 0x2022, 0x2026, 0x2039, 0x203A, 0x20AC};
constexpr int kLatin = (0x7F - 0x20) + (0x180 - 0xA0);
constexpr int kCyrillic = 0x460 - 0x400;
constexpr int kCharsetSize = kLatin + kCyrillic + static_cast<int>(sizeof(kExtra) / sizeof(kExtra[0]));

uint32_t charset(int i) {
  if (i < 0x7F - 0x20) return 0x20 + i;
  i -= 0x7F - 0x20;
  if (i < 0x180 - 0xA0) return 0xA0 + i;
  i -= 0x180 - 0xA0;
  if (i < kCyrillic) return 0x400 + i;
  return kExtra[i - kCyrillic];
}

// Characters worth kerning against each other: fewer than the built-in
// fonts kern (every letter), because each pair is looked up one by one.
bool kerned(uint32_t cp) {
  if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
  if (cp < 0x80) return std::strchr(".,;:!?'\"-()[]", static_cast<int>(cp)) != nullptr;
  constexpr uint32_t kLithuanian[] = {0x104, 0x105, 0x10C, 0x10D, 0x116, 0x117, 0x118, 0x119, 0x12E,
                                      0x12F, 0x160, 0x161, 0x16A, 0x16B, 0x172, 0x173, 0x17D, 0x17E};
  for (uint32_t c : kLithuanian) {
    if (cp == c) return true;
  }
  return cp == 0xAB || cp == 0xBB || (cp >= 0x2013 && cp <= 0x201E) || cp == 0x2026;
}

constexpr int kKernMin26_6 = 32;  // half a pixel; smaller kerning is dropped, as built in

}  // namespace

struct TtfFace::Info {
  stbtt_fontinfo font;
};

TtfFace::TtfFace() : info_(new(std::nothrow) Info) {}

TtfFace::~TtfFace() {
  delete info_;
  delete[] pairs_;
}

bool TtfFace::load(const uint8_t* data, size_t size) {
  loaded_ = false;
  delete[] pairs_;
  pairs_ = nullptr;
  pair_count_ = -1;
  if (info_ == nullptr || data == nullptr || size < 12) return false;
  const int offset = stbtt_GetFontOffsetForIndex(data, 0);
  if (offset < 0 || static_cast<size_t>(offset) >= size) return false;
  if (!stbtt_InitFont(&info_->font, data, offset)) return false;
  if (stbtt_FindGlyphIndex(&info_->font, 'a') == 0) return false;  // not a font for text
  loaded_ = true;
  return true;
}

void TtfFace::find_kerning() {
  if (pair_count_ >= 0) return;
  pair_count_ = 0;
  int idx[kCharsetSize];
  int glyph[kCharsetSize];
  int n = 0;
  for (int i = 0; i < kCharsetSize; ++i) {
    const uint32_t cp = charset(i);
    if (!kerned(cp)) continue;
    const int g = stbtt_FindGlyphIndex(&info_->font, static_cast<int>(cp));
    if (g == 0) continue;
    idx[n] = i;
    glyph[n] = g;
    ++n;
  }
  // Two passes: count, then keep. Font units; sizes filter their own.
  for (int pass = 0; pass < 2; ++pass) {
    int count = 0;
    for (int a = 0; a < n; ++a) {
      for (int b = 0; b < n; ++b) {
        const int k = stbtt_GetGlyphKernAdvance(&info_->font, glyph[a], glyph[b]);
        if (k == 0) continue;
        if (pass == 1)
          pairs_[count] = {static_cast<uint16_t>(idx[a]), static_cast<uint16_t>(idx[b]),
                           static_cast<int16_t>(std::clamp(k, -32768, 32767))};
        ++count;
      }
    }
    if (pass == 0) {
      if (count == 0) return;
      pairs_ = new (std::nothrow) Pair[count];
      if (pairs_ == nullptr) return;
    } else {
      pair_count_ = count;
    }
  }
}

void RasterFont::clear() {
  delete[] glyphs_;
  delete[] bitmap_;
  delete[] kerning_;
  glyphs_ = nullptr;
  bitmap_ = nullptr;
  kerning_ = nullptr;
  font_ = {};
}

bool RasterFont::build(TtfFace& face, int px, const char* family) {
  clear();
  if (!face.loaded() || px < 6 || px > 120) return false;
  const stbtt_fontinfo& info = face.info_->font;
  const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(px));
  int asc, desc, gap;
  stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);

  // First pass: which characters exist, and how much bitmap they need.
  int count = 0;
  size_t bytes = 0;
  int16_t slot[kCharsetSize];  // glyph index in this font per character, or -1
  for (int i = 0; i < kCharsetSize; ++i) {
    slot[i] = -1;
    const int g = stbtt_FindGlyphIndex(&info, static_cast<int>(charset(i)));
    if (g == 0 && charset(i) != ' ') continue;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&info, g, scale, scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w > 255 || h > 255 || x0 < -128 || x0 > 127 || -y0 < -128 || -y0 > 127) continue;
    bytes += (static_cast<size_t>(std::max(w, 0)) * std::max(h, 0) + 3) / 4;
    slot[i] = static_cast<int16_t>(count++);
  }
  if (count == 0) return false;
  glyphs_ = new (std::nothrow) Glyph[count];
  bitmap_ = new (std::nothrow) uint8_t[bytes + 1];
  int max_w = 0;
  for (int i = 0; i < kCharsetSize; ++i) {
    if (slot[i] < 0) continue;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&info, stbtt_FindGlyphIndex(&info, static_cast<int>(charset(i))), scale, scale, &x0, &y0,
                            &x1, &y1);
    max_w = std::max(max_w, (x1 - x0) * (y1 - y0));
  }
  uint8_t* coverage = new (std::nothrow) uint8_t[std::max(max_w, 1)];
  if (glyphs_ == nullptr || bitmap_ == nullptr || coverage == nullptr) {
    delete[] coverage;
    clear();
    return false;
  }

  // Second pass: render each glyph and pack its coverage, 2 bits a pixel.
  size_t offset = 0;
  for (int i = 0; i < kCharsetSize; ++i) {
    if (slot[i] < 0) continue;
    const int g = stbtt_FindGlyphIndex(&info, static_cast<int>(charset(i)));
    int advance, lsb;
    stbtt_GetGlyphHMetrics(&info, g, &advance, &lsb);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&info, g, scale, scale, &x0, &y0, &x1, &y1);
    const int w = std::max(x1 - x0, 0);
    const int h = std::max(y1 - y0, 0);
    if (w > 0 && h > 0) stbtt_MakeGlyphBitmap(&info, coverage, w, h, w, scale, scale, g);
    Glyph& out = glyphs_[slot[i]];
    out = {charset(i),
           static_cast<uint32_t>(offset),
           static_cast<uint8_t>(w),
           static_cast<uint8_t>(h),
           static_cast<int8_t>(x0),
           static_cast<int8_t>(-y0),
           static_cast<uint16_t>(std::clamp(static_cast<int>(advance * scale * 64.0f + 0.5f), 0, 65535))};
    uint8_t acc = 0;
    int bits = 0;
    for (int p = 0; p < w * h; ++p) {
      acc = static_cast<uint8_t>((acc << 2) | (coverage[p] >> 6));
      bits += 2;
      if (bits == 8) {
        bitmap_[offset++] = acc;
        acc = 0;
        bits = 0;
      }
    }
    if (bits > 0) bitmap_[offset++] = static_cast<uint8_t>(acc << (8 - bits));
  }
  delete[] coverage;

  // Kerning at this size, between the characters this font has.
  face.find_kerning();
  int kept = 0;
  for (int pass = 0; pass < 2; ++pass) {
    kept = 0;
    for (int p = 0; p < face.pair_count_; ++p) {
      const TtfFace::Pair& pair = face.pairs_[p];
      const int adjust = static_cast<int>(pair.units * scale * 64.0f + (pair.units < 0 ? -0.5f : 0.5f));
      if (std::abs(adjust) < kKernMin26_6 || slot[pair.left] < 0 || slot[pair.right] < 0) continue;
      if (pass == 1) {
        kerning_[kept] = {static_cast<uint16_t>(slot[pair.left]), static_cast<uint16_t>(slot[pair.right]),
                          static_cast<int16_t>(adjust)};
      }
      ++kept;
    }
    if (pass == 0 && kept > 0) {
      kerning_ = new (std::nothrow) KernPair[kept];
      if (kerning_ == nullptr) break;
    }
  }
  if (kerning_ == nullptr) kept = 0;
  std::sort(kerning_, kerning_ + kept, [](const KernPair& a, const KernPair& b) {
    return a.left != b.left ? a.left < b.left : a.right < b.right;
  });

  std::snprintf(family_, sizeof(family_), "%s", family != nullptr ? family : "");
  const auto clamp8 = [](float v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(v + 0.999f), 0, 255)); };
  const uint16_t question = slot['?' - 0x20] >= 0 ? static_cast<uint16_t>(slot['?' - 0x20]) : 0;
  font_ = {family_,
           static_cast<uint8_t>(px),
           clamp8(asc * scale),
           clamp8(-desc * scale),
           clamp8((asc - desc + gap) * scale),
           glyphs_,
           static_cast<uint16_t>(count),
           bitmap_,
           kerning_,
           static_cast<uint16_t>(kept),
           question};
  return true;
}

}  // namespace vellum

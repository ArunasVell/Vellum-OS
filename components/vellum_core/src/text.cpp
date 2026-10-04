#include "vellum/text.h"

#include <algorithm>

namespace vellum {

uint16_t Font::find(uint32_t cp) const {
  const Glyph* end = glyphs + glyph_count;
  const Glyph* g = std::lower_bound(glyphs, end, cp, [](const Glyph& a, uint32_t c) { return a.codepoint < c; });
  return (g != end && g->codepoint == cp) ? static_cast<uint16_t>(g - glyphs) : fallback;
}

int Font::kern(uint16_t left, uint16_t right) const {
  const KernPair* end = kerning + kerning_count;
  const KernPair* k = std::lower_bound(
      kerning, end, KernPair{left, right, 0},
      [](const KernPair& a, const KernPair& b) { return a.left != b.left ? a.left < b.left : a.right < b.right; });
  return (k != end && k->left == left && k->right == right) ? k->adjust : 0;
}

uint32_t decode_utf8(std::string_view s, size_t& i) {
  constexpr uint32_t kReplacement = 0xFFFD;
  const auto b0 = static_cast<uint8_t>(s[i]);
  int extra;
  uint32_t cp;
  if (b0 < 0x80) {
    ++i;
    return b0;
  } else if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    cp = b0 & 0x07;
  } else {
    ++i;
    return kReplacement;
  }
  if (i + extra >= s.size()) {  // truncated sequence
    ++i;
    return kReplacement;
  }
  for (int k = 1; k <= extra; ++k) {
    const auto b = static_cast<uint8_t>(s[i + k]);
    if ((b & 0xC0) != 0x80) {
      ++i;
      return kReplacement;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  i += extra + 1;
  return cp;
}

namespace {

constexpr uint8_t kInkThreshold = 2;  // coverage 2 or 3 of 3 becomes black on a 1-bit panel

// Walks the glyphs of `text`, calling fn(glyph, pen_x) with kerning applied.
template <typename Fn>
int32_t walk(const Font& font, std::string_view text, int32_t x, Fn&& fn) {
  size_t i = 0;
  int prev = -1;
  while (i < text.size()) {
    const uint16_t g = font.find(decode_utf8(text, i));
    if (prev >= 0) x += font.kern(static_cast<uint16_t>(prev), g);
    fn(font.glyphs[g], x);
    x += font.glyphs[g].advance;
    prev = g;
  }
  return x;
}

}  // namespace

int32_t text_width(const Font& font, std::string_view text) {
  return walk(font, text, 0, [](const Glyph&, int32_t) {});
}

int32_t draw_text(Framebuffer& fb, const Font& font, int32_t x, int y, std::string_view text, Color c) {
  return walk(font, text, x, [&](const Glyph& g, int32_t pen) {
    if (g.width == 0) return;
    fb.draw_mask(((pen + 32) >> 6) + g.left, y - g.top, g.width, g.height, font.bitmap + g.offset, kInkThreshold, c);
  });
}

namespace {

struct Word {
  uint32_t begin;
  uint32_t end;
  int32_t width;  // includes the hyphen when there is one
  bool hyphen;    // the word continues on the next line
};

constexpr int kMaxBreaks = 24;

constexpr int kMaxWords = 48;

bool is_space(char c) { return c == ' '; }

uint32_t skip_spaces(std::string_view s, uint32_t i) {
  while (i < s.size() && is_space(s[i])) ++i;
  return i;
}

// Longest prefix of s[begin, end) that fits in `room`, at least one character.
uint32_t fit_prefix(const Font& font, std::string_view s, uint32_t begin, uint32_t end, int32_t room) {
  size_t i = begin;
  uint32_t last = begin;
  while (i < end) {
    size_t next = i;
    decode_utf8(s, next);
    if (last != begin && text_width(font, s.substr(begin, next - begin)) > room) break;
    last = static_cast<uint32_t>(next);
    i = next;
  }
  return last;
}

// End of the longest prefix of s[begin, end) that stops just after a hyphen
// or dash and fits in `room`, or 0 when there is none.
uint32_t dash_break(const Font& font, std::string_view s, uint32_t begin, uint32_t end, int32_t room) {
  uint32_t best = 0;
  size_t i = begin;
  while (i < end) {
    const uint32_t cp = decode_utf8(s, i);
    const bool dash = cp == '-' || cp == 0x2013 || cp == 0x2014;
    if (!dash || i >= end) continue;
    if (text_width(font, s.substr(begin, i - begin)) > room) break;
    best = static_cast<uint32_t>(i);
  }
  return best;
}

// End of the longest hyphenated prefix of s[begin, end) that fits in `room`
// with its hyphen, or 0 when there is none.
uint32_t hyphen_break(const Font& font, const Hyphenator& h, std::string_view s, uint32_t begin, uint32_t end,
                      int32_t room) {
  uint16_t breaks[kMaxBreaks];
  const int n = hyphenate(h, s.substr(begin, end - begin), breaks, kMaxBreaks);
  const int32_t hyphen = text_width(font, "-");
  for (int k = n - 1; k >= 0; --k) {
    if (text_width(font, s.substr(begin, breaks[k])) + hyphen <= room) return begin + breaks[k];
  }
  return 0;
}

}  // namespace

TextPos typeset_page(const PageStyle& style, const char* const* paragraphs, uint32_t count, TextPos pos,
                     Framebuffer* fb) {
  const Font& font = *style.font;
  const int32_t line_width = style.area.w * 64;
  const int32_t space = font.glyphs[font.find(' ')].advance * style.word_space / 100;
  const int line_height = style.line_height > 0 ? style.line_height : font.line_height;
  int baseline = style.area.y + font.ascent;
  const int bottom = style.area.y + style.area.h;

  while (pos.paragraph < count && baseline + font.descent <= bottom) {
    const std::string_view text(paragraphs[pos.paragraph]);
    const bool first_line = pos.offset == 0;
    const int32_t indent = (first_line && pos.paragraph > 0) ? style.indent * 64 : 0;

    Word words[kMaxWords];
    int n = 0;
    int32_t natural = indent;
    uint32_t i = skip_spaces(text, pos.offset);
    while (i < text.size() && n < kMaxWords) {
      uint32_t end = i;
      while (end < text.size() && !is_space(text[end])) ++end;
      const int32_t w = text_width(font, text.substr(i, end - i));
      const int32_t needed = natural + (n > 0 ? space : 0) + w;
      if (needed > line_width) {
        // Compounds and dashed phrases may break after their dash.
        const int32_t room = line_width - natural - (n > 0 ? space : 0);
        uint32_t cut = dash_break(font, text, i, end, room);
        bool hyphen = false;
        if (cut == 0 && style.hyphenator != nullptr) {
          cut = hyphen_break(font, *style.hyphenator, text, i, end, room);
          hyphen = cut != 0;
        }
        if (cut == 0 && n > 0) break;
        // A single word wider than the line is split where it overflows.
        if (cut == 0) cut = fit_prefix(font, text, i, end, room);
        const int32_t cut_w = text_width(font, text.substr(i, cut - i)) + (hyphen ? text_width(font, "-") : 0);
        natural += (n > 0 ? space : 0) + cut_w;
        words[n++] = {i, cut, cut_w, hyphen};
        i = cut;
        break;
      }
      words[n++] = {i, end, w, false};
      natural = needed;
      i = skip_spaces(text, end);
    }

    const bool last_line = i >= text.size();
    if (fb != nullptr) {
      int32_t x = style.area.x * 64 + indent;
      const int gaps = n - 1;
      const int32_t extra = (!style.justify || last_line || gaps <= 0) ? 0 : line_width - natural;
      for (int k = 0; k < n; ++k) {
        const int32_t after =
            draw_text(*fb, font, x, baseline, text.substr(words[k].begin, words[k].end - words[k].begin));
        if (words[k].hyphen) draw_text(*fb, font, after, baseline, "-");
        // Spread the leftover space evenly; earlier gaps take the remainder.
        if (k < gaps) x += words[k].width + space + extra / gaps + (k < extra % gaps ? 1 : 0);
      }
    }

    pos = last_line ? TextPos{pos.paragraph + 1, 0} : TextPos{pos.paragraph, i};
    baseline += line_height;
  }
  return pos;
}

}  // namespace vellum

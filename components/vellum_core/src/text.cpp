#include "vellum/text.h"

#include <algorithm>

namespace vellum {

uint16_t Font::find(uint32_t cp) const {
  // Printable ASCII usually starts the table in order: look there first.
  if (cp >= 0x20 && cp < 0x7F && cp - 0x20 < glyph_count && glyphs[cp - 0x20].codepoint == cp) {
    return static_cast<uint16_t>(cp - 0x20);
  }
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

// Characters that take no room. Fonts often lack them; they are left out
// rather than drawn as missing.
bool is_zero_width(uint32_t cp) {
  return cp < 0x20 || cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || cp == 0x2060 || cp == 0xFEFF ||
         (cp >= 0x0300 && cp <= 0x036F);  // combining accents
}

// A character the font lacks is a small box this wide (1/64 px).
int32_t box_advance(const Font& font) { return font.size * 64 * 6 / 10; }

// Walks the glyphs of `text`, calling fn(glyph, pen_x) with kerning applied.
// A character the font lacks comes as nullptr: a box of box_advance().
template <typename Fn>
int32_t walk(const Font& font, std::string_view text, int32_t x, Fn&& fn) {
  size_t i = 0;
  int prev = -1;
  while (i < text.size()) {
    const uint32_t cp = decode_utf8(text, i);
    const uint16_t g = font.find(cp);
    if (g == font.fallback && font.glyphs[g].codepoint != cp) {
      if (is_zero_width(cp)) continue;
      fn(nullptr, x);
      x += box_advance(font);
      prev = -1;
      continue;
    }
    if (prev >= 0) x += font.kern(static_cast<uint16_t>(prev), g);
    fn(&font.glyphs[g], x);
    x += font.glyphs[g].advance;
    prev = g;
  }
  return x;
}

// The box drawn for a missing character at pen position `pen`.
Rect missing_box(const Font& font, int32_t pen, int y) {
  const int w = box_advance(font) / 64;
  const int h = font.ascent * 7 / 10;
  return {((pen + 32) >> 6) + 1, y - h, w - 3, h};
}

// A glyph slanted for italics: each row is shifted right by a fifth of its
// height above the baseline.
void draw_slanted(Framebuffer& fb, const Font& font, const Glyph& g, int x, int y, Color c) {
  const uint8_t* mask = font.bitmap + g.offset;
  for (int row = 0; row < g.height; ++row) {
    const int py = y - g.top + row;
    const int shift = ((y - py) * 13) >> 6;
    for (int col = 0; col < g.width; ++col) {
      const size_t i = static_cast<size_t>(row) * g.width + col;
      if (((mask[i >> 2] >> (6 - 2 * (i & 3))) & 3) >= kInkThreshold) fb.set_pixel(x + g.left + col + shift, py, c);
    }
  }
}

}  // namespace

int32_t text_width(const Font& font, std::string_view text) {
  return walk(font, text, 0, [](const Glyph*, int32_t) {});
}

int32_t draw_text(Framebuffer& fb, const Font& font, int32_t x, int y, std::string_view text, Color c) {
  return walk(font, text, x, [&](const Glyph* g, int32_t pen) {
    if (g == nullptr) return fb.draw_rect(missing_box(font, pen, y), c);
    if (g->width == 0) return;
    fb.draw_mask(((pen + 32) >> 6) + g->left, y - g->top, g->width, g->height, font.bitmap + g->offset, kInkThreshold,
                 c);
  });
}

int32_t draw_styled(Framebuffer& fb, const Font& font, int32_t x, int y, std::string_view text, uint8_t flags,
                    Color c) {
  const bool bold = (flags & StyleRun::kBold) != 0;
  if ((flags & StyleRun::kItalic) == 0) {
    if (bold) draw_text(fb, font, x + 64, y, text, c);
    return draw_text(fb, font, x, y, text, c);
  }
  return walk(font, text, x, [&](const Glyph* g, int32_t pen) {
    if (g == nullptr) return fb.draw_rect(missing_box(font, pen, y), c);
    for (int k = bold ? 1 : 0; k >= 0; --k) draw_slanted(fb, font, *g, ((pen + 32) >> 6) + k, y, c);
  });
}

ParagraphMarks read_marks(std::string_view p) {
  ParagraphMarks m;
  for (; m.length < p.size() && is_mark(p[m.length]); ++m.length) {
    switch (p[m.length]) {
      case kHeadingMark:
        m.heading = true;
        break;
      case kPictureMark:
        m.picture = true;
        ++m.length;
        return m;  // the rest is alt text
      case kRuleMark:
        m.rule = true;
        break;
      case kCenterMark:
        m.align = 1;
        break;
      case kRightMark:
        m.align = 2;
        break;
      case kNoIndentMark:
        m.first = -1;
        break;
      case kIndentMark:
        m.first = 1;
        break;
      case kLevelMark:
        if (m.level < 255) ++m.level;
        break;
      case kItemMark:
        m.item = true;
        break;
      case kRowMark:
        m.row = true;
        break;
      default:
        break;
    }
  }
  return m;
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

constexpr int kMaxLevels = 4;  // deeper lists are set in no further

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

// Letters and digits, for focus reading; punctuation and symbols are not.
bool is_letter(uint32_t cp) {
  if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
  return cp >= 0xC0 && cp != 0xD7 && cp != 0xF7 && !(cp >= 0x2000 && cp <= 0x2BFF) && !(cp >= 0x3000 && cp <= 0x303F);
}

// Focus reading: the bytes of `word` drawn in bold, up to the end of its
// first two letters in five (at least one).
size_t focus_length(std::string_view word) {
  int letters = 0;
  for (size_t i = 0; i < word.size();) {
    if (is_letter(decode_utf8(word, i))) ++letters;
  }
  if (letters == 0) return 0;
  int want = std::max(1, (letters * 2 + 2) / 5);
  size_t i = 0;
  while (i < word.size() && want > 0) {
    if (is_letter(decode_utf8(word, i))) --want;
  }
  return i;
}

// Draws word text[begin, end) of `paragraph` from pen position x, with its
// emphasis a run at a time: a word may change part way ("mill.¹", the
// note's number a link). Links are underlined lightly. Returns the
// emphasis of its end, for a hyphen after it.
uint8_t draw_word(Framebuffer& fb, const PageStyle& style, uint32_t paragraph, std::string_view text, uint32_t begin,
                  uint32_t end, int32_t x, int baseline) {
  const Font& font = *style.font;
  const StyleRun* stop = style.runs + style.run_count;
  const StyleRun* next = style.run_count == 0 ? stop
                                              : std::upper_bound(style.runs, stop, TextPos{paragraph, begin},
                                                                 [](TextPos p, const StyleRun& r) { return p < r.at; });
  uint8_t flags = next == style.runs || style.run_count == 0 ? 0 : (next - 1)->flags;
  const int under = std::max(2, font.descent / 3);
  uint8_t look = 0;
  for (uint32_t from = begin; from < end;) {
    uint32_t to = end;
    if (next != stop && next->at.paragraph == paragraph && next->at.offset < end) to = std::max(from, next->at.offset);
    const uint8_t f = style.publisher ? flags : flags & StyleRun::kLink;
    look = f & (StyleRun::kBold | StyleRun::kItalic);
    if (to > from) {
      const int32_t after = draw_styled(fb, font, x, baseline, text.substr(from, to - from), look);
      if (f & StyleRun::kLink) {
        for (int px = (x + 32) >> 6; px < (after + 32) >> 6; px += 2) fb.set_pixel(px, baseline + under, Color::Black);
      }
      x = after;
    }
    from = to;
    if (to < end) flags = (next++)->flags;
  }
  return look;
}

const Picture* find_picture(const PageStyle& style, uint32_t paragraph) {
  const Picture* end = style.pictures + style.picture_count;
  const Picture* p = std::lower_bound(style.pictures, end, paragraph,
                                      [](const Picture& a, uint32_t para) { return a.paragraph < para; });
  return p != end && p->paragraph == paragraph ? p : nullptr;
}

// A picture that cannot be shown: a frame with its alt text, cut to fit,
// or crossed when it has none.
void draw_placeholder(Framebuffer& fb, const Font& font, Rect r, std::string_view alt) {
  fb.draw_rect(r, Color::Black);
  const int room = r.w - 24;
  while (!alt.empty() && text_width_px(font, alt) > room) {
    size_t cut = alt.size() - 1;
    while (cut > 0 && (static_cast<uint8_t>(alt[cut]) & 0xC0) == 0x80) --cut;
    alt = alt.substr(0, cut);
  }
  if (!alt.empty()) {
    const int w = text_width_px(font, alt);
    draw_text(fb, font, (r.x + (r.w - w) / 2) * 64, r.y + (r.h + font.ascent - font.descent) / 2, alt);
    return;
  }
  const int steps = std::max(r.w, r.h);
  for (int t = 0; t < steps; t += 2) {
    const int x = r.x + t * r.w / steps;
    fb.set_pixel(x, r.y + t * r.h / steps, Color::Black);
    fb.set_pixel(x, r.y + r.h - 1 - t * r.h / steps, Color::Black);
  }
}

}  // namespace

int PageWords::at(int x, int y, int slop) const {
  int best = -1;
  int best_d = slop + 1;
  for (int i = 0; i < count; ++i) {
    const WordBox& b = words[i];
    if (y < b.y - slop / 2 || y >= b.y + b.h + slop / 2) continue;
    const int d = x < b.x ? b.x - x : x >= b.x + b.w ? x - (b.x + b.w) + 1 : 0;
    if (d < best_d) {
      best = i;
      best_d = d;
    }
  }
  return best;
}

Rect picture_box(const PageStyle& style, const Picture* p, int top) {
  const int line = style.line_height > 0 ? style.line_height : style.font->line_height;
  if (p == nullptr || p->w == 0 || p->h == 0) return {style.area.x, top, style.area.w, 2 * line};
  int64_t w = int64_t{p->w} * 2;
  int64_t h = int64_t{p->h} * 2;
  if (w > style.area.w) {
    h = h * style.area.w / w;
    w = style.area.w;
  }
  if (h > style.area.h) {
    w = w * style.area.h / h;
    h = style.area.h;
  }
  const int pw = std::max<int>(1, static_cast<int>(w));
  const int ph = std::max<int>(1, static_cast<int>(h));
  return {style.area.x + (style.area.w - pw) / 2, top, pw, ph};
}

TextPos typeset_page(const PageStyle& style, const char* const* paragraphs, uint32_t count, TextPos pos,
                     Framebuffer* fb, PageWords* boxes) {
  if (boxes != nullptr) boxes->count = 0;
  const Font& font = *style.font;
  const int32_t full_width = style.area.w * 64;
  const int32_t space = font.glyphs[font.find(' ')].advance * style.word_space / 100;
  const int line_height = style.line_height > 0 ? style.line_height : font.line_height;
  int baseline = style.area.y + font.ascent;
  const int bottom = style.area.y + style.area.h;
  const TextPos start = pos;

  // The paragraph being set; measured once, not once a line.
  uint32_t text_of = count;
  std::string_view text;
  ParagraphMarks marks;
  int32_t hang = 0;  // a list item's later lines start after its bullet
  while (pos.paragraph < count && baseline + font.descent <= bottom) {
    if (text_of != pos.paragraph) {
      text_of = pos.paragraph;
      text = paragraphs[pos.paragraph];
      marks = read_marks(text);
      hang = 0;
      if (marks.item) {
        const uint32_t b = skip_spaces(text, marks.length);
        uint32_t e = b;
        while (e < text.size() && !is_space(text[e])) ++e;
        hang = text_width(font, text.substr(b, e - b)) + space;
      }
    }
    const int top = baseline - font.ascent;

    if (marks.picture) {
      const Picture* pic = style.pictures != nullptr ? find_picture(style, pos.paragraph) : nullptr;
      Rect box = picture_box(style, pic, pos == start ? top : top + line_height / 4);
      const bool large = box.h > style.area.h / 2;
      if (!(pos == start) && (large || box.y + box.h > bottom)) break;  // on the next page
      if (large) box.y = style.area.y + (style.area.h - box.h) / 2;     // a page to itself
      if (fb != nullptr) {
        const bool shown = pic != nullptr && pic->w > 0 && style.draw_picture != nullptr &&
                           style.draw_picture(style.picture_ctx, *fb, *pic, box);
        if (!shown) draw_placeholder(*fb, font, box, text.substr(marks.length));
      }
      pos = {pos.paragraph + 1, 0};
      if (large) break;
      baseline = box.y + box.h + line_height / 2 + font.ascent;
      continue;
    }
    if (marks.rule) {
      // A scene break: a short line across the middle of a blank line.
      if (fb != nullptr) {
        const int w = style.area.w / 4;
        fb->hline(style.area.x + (style.area.w - w) / 2, baseline - font.ascent / 3, w, Color::Black);
      }
      pos = {pos.paragraph + 1, 0};
      baseline += line_height;
      continue;
    }

    const bool heading = marks.heading;
    const bool first_line = pos.offset == 0;
    const int align = heading ? 1 : style.publisher ? marks.align : 0;
    // Indent every paragraph but the first and those after a heading, unless
    // the book (or Settings) says otherwise.
    const bool after_heading = pos.paragraph > 0 && paragraphs[pos.paragraph - 1][0] == kHeadingMark;
    bool indent_first = pos.paragraph > 0 && !heading && !after_heading;
    if (style.indent_rule == IndentRule::Never) {
      indent_first = false;
    } else if (style.indent_rule == IndentRule::Book && style.publisher && marks.first != 0) {
      indent_first = marks.first > 0 && !heading;
    }
    if (align != 0 || marks.row || marks.item) indent_first = false;
    const int32_t inset = std::min<int>(marks.level, kMaxLevels) * font.size * 64;
    const int32_t left = inset + (first_line ? (indent_first ? style.indent * 64 : 0) : hang);
    const int32_t line_width = std::max(full_width / 4, full_width - left);

    Word words[kMaxWords];
    int n = 0;
    int32_t natural = 0;
    uint32_t i = skip_spaces(text, first_line ? marks.length : pos.offset);
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
    if (fb != nullptr || boxes != nullptr) {
      // Headings and centred paragraphs are centred; other lines justified
      // unless ragged right is chosen (table rows always are).
      int32_t x = style.area.x * 64 + left;
      if (align == 1) x += (line_width - natural) / 2;
      if (align == 2) x += line_width - natural;
      const int gaps = n - 1;
      const bool ragged = align != 0 || marks.row || !style.justify || last_line || gaps <= 0;
      const int32_t extra = ragged ? 0 : line_width - natural;
      for (int k = 0; k < n; ++k) {
        const std::string_view word = text.substr(words[k].begin, words[k].end - words[k].begin);
        if (fb != nullptr) {
          const uint8_t look = draw_word(*fb, style, pos.paragraph, text, words[k].begin, words[k].end, x, baseline);
          if (words[k].hyphen) {
            draw_styled(*fb, font, x + words[k].width - text_width(font, "-"), baseline, "-", look);
          }
          // Focus reading: the word's start again, a pixel on, for bold. Not
          // for the second half of a word broken across lines.
          const bool word_start = words[k].begin <= marks.length || is_space(text[words[k].begin - 1]);
          if (style.focus && word_start && (look & StyleRun::kBold) == 0) {
            if (const size_t lead = focus_length(word); lead > 0) {
              draw_styled(*fb, font, x + 64, baseline, word.substr(0, lead), look & StyleRun::kItalic);
            }
          }
        }
        if (boxes != nullptr && boxes->count < PageWords::kMax) {
          const int left_px = (x + 32) >> 6;
          boxes->words[boxes->count++] = {static_cast<int16_t>(left_px),
                                          static_cast<int16_t>(baseline - font.ascent),
                                          static_cast<int16_t>(((x + words[k].width + 32) >> 6) - left_px),
                                          static_cast<int16_t>(font.ascent + font.descent),
                                          static_cast<int16_t>(baseline),
                                          words[k].hyphen,
                                          x,
                                          {pos.paragraph, words[k].begin},
                                          words[k].end};
        }
        // Spread the leftover space evenly; earlier gaps take the remainder.
        if (k < gaps) x += words[k].width + space + extra / gaps + (k < extra % gaps ? 1 : 0);
      }
    }

    pos = last_line ? TextPos{pos.paragraph + 1, 0} : TextPos{pos.paragraph, i};
    baseline += line_height;
    // Without indents, paragraphs are set apart instead.
    if (last_line && style.indent_rule == IndentRule::Never && !heading) baseline += line_height / 2;
  }
  return pos;
}

}  // namespace vellum

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

// Marks: control bytes at the start of a paragraph, never in its text, that
// say what kind of block it is and how to set it. kHeadingMark comes first
// when there is one; the others follow in any order.
//
// A heading: set centred, and the paragraph after it is not indented.
constexpr char kHeadingMark = '\x01';
constexpr char kPictureMark = '\x02';   // a picture (PageStyle::pictures); the rest is its alt text
constexpr char kRuleMark = '\x03';      // a horizontal rule: a short centred line
constexpr char kCenterMark = '\x04';    // the book centres it (publisher styles)
constexpr char kRightMark = '\x05';     // the book sets it flush right
constexpr char kNoIndentMark = '\x06';  // the book sets its first line flush
constexpr char kIndentMark = '\x07';    // the book indents its first line
constexpr char kLevelMark = '\x0E';     // set in one level (a list or quotation); repeated for deeper ones
constexpr char kItemMark = '\x0F';      // a list item: its first word, the bullet or number, hangs
constexpr char kRowMark = '\x10';       // a table row: cells apart, set ragged, never indented

// Whether a byte is one of the marks.
inline bool is_mark(char c) { return c > '\0' && c < ' '; }

// The marks of a paragraph, read.
struct ParagraphMarks {
  uint32_t length = 0;  // bytes of marks before the text
  bool heading = false;
  bool picture = false;
  bool rule = false;
  bool item = false;
  bool row = false;
  uint8_t align = 0;  // 0 as the reader sets it, 1 centred, 2 flush right
  int8_t first = 0;   // first-line indent: 0 as usual, -1 none, 1 indented
  uint8_t level = 0;
};
ParagraphMarks read_marks(std::string_view paragraph);

// Inline emphasis: from `at` on, until the next run, words are set with
// these flags. Runs are in text order.
struct StyleRun {
  static constexpr uint8_t kBold = 1;
  static constexpr uint8_t kItalic = 2;
  static constexpr uint8_t kLink = 4;  // underlined lightly
  TextPos at;
  uint8_t flags = 0;
};

// Draws `text` like draw_text, in bold (drawn twice, a pixel apart: the
// fonts have no bold) and/or slanted for italics. The pen advances as for
// plain text, so emphasis never changes the layout.
int32_t draw_styled(Framebuffer& fb, const Font& font, int32_t x, int y, std::string_view text, uint8_t flags,
                    Color c = Color::Black);

// A picture in a chapter: the paragraph that holds it (kPictureMark), the
// archive entry with its data and its size in pixels. A picture without a
// size cannot be shown and gets a placeholder box with its alt text.
struct Picture {
  uint32_t paragraph = 0;
  int32_t entry = -1;
  uint16_t w = 0;
  uint16_t h = 0;
};

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

// Paragraph indents: as the book (and the reader's usual rule) has them,
// always, or never (then paragraphs are set apart by half a line instead).
enum class IndentRule : uint8_t { Book, Always, Never };

// Draws picture p into r; false when it cannot (a placeholder is drawn).
using PictureFn = bool (*)(void* ctx, Framebuffer& fb, const Picture& p, Rect r);

struct PageStyle {
  const Font* font;
  Rect area;                     // where text may go, logical pixels
  int indent;                    // first-line indent of every paragraph but the first, px
  const Hyphenator* hyphenator;  // language rules for breaking words; nullptr = none
  int line_height = 0;           // baseline to baseline, px; 0 = the font's own
  int word_space = 100;          // narrowest space between words, percent of the font's space
  bool justify = true;           // false = ragged right
  // The book's own look: alignment, indents (kCenterMark...) and emphasis.
  // Off, paragraphs are set alike; headings stay centred, links underlined.
  bool publisher = true;
  IndentRule indent_rule = IndentRule::Book;
  // Focus reading: the first part of every word in bold, to lead the eye.
  bool focus = false;
  // The chapter's emphasis and pictures, in text order.
  const StyleRun* runs = nullptr;
  uint32_t run_count = 0;
  const Picture* pictures = nullptr;
  uint32_t picture_count = 0;
  PictureFn draw_picture = nullptr;  // nullptr: placeholders only
  void* picture_ctx = nullptr;
};

// Where picture p goes when its top is at `top`: scaled to the text area
// (at most twice its own size, at most a page high) and centred. `p` may
// be nullptr or have no size: then a placeholder box two lines high.
Rect picture_box(const PageStyle& style, const Picture* p, int top);

// Lays out one page of `paragraphs` from `start`: justified lines (when
// style.justify), the last line of each paragraph set ragged, words hyphenated when the next one does
// not fit. Pictures, rules, lists and table rows follow their marks. A
// picture that does not fit the rest of the page starts the next one; a
// picture taller than half a page has a page to itself. Draws when `fb` is not null, so the
// same call paginates (fb = nullptr) and renders. Returns where the next
// page starts; that equals {count, 0} once all text is placed. When `words`
// is not null it receives the page's word boxes (up to PageWords::kMax).
TextPos typeset_page(const PageStyle& style, const char* const* paragraphs, uint32_t count, TextPos start,
                     Framebuffer* fb, PageWords* words = nullptr);

}  // namespace vellum

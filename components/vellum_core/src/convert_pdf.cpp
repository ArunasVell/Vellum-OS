// PDF books that hold text, reflowed: their words are taken from the pages
// and set again as paragraphs, so they read like any other book. Pictures,
// columns and page design are left behind; scanned pages (pictures of
// text) and password-protected files cannot be read.
//
// Reading a page: its content stream is inflated and run through just
// enough of the PDF text operators to know each string's position, size and
// characters (from the font's ToUnicode map, its encoding or glyph names).
// Strings become lines by position, lines become paragraphs by spacing,
// indents and line ends; large type becomes headings, the book's outline
// its contents, and page numbers and running heads are left out.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "convert_internal.h"
#include "miniz.h"
#include "vellum/charset.h"

namespace vellum::convert {
namespace {

constexpr uint32_t kMaxObjects = 32768;
constexpr uint32_t kMaxPages = 8192;
constexpr uint32_t kSlotBytes = 40 * 1024;
constexpr int kSlots = 5;
constexpr uint32_t kObjStmBytes = 160 * 1024;
constexpr uint32_t kContentBytes = 256 * 1024;
constexpr uint32_t kChunk = 16 * 1024;
constexpr uint32_t kPoolWords = 32 * 1024;
constexpr int kMaxFonts = 48;
constexpr int kMaxRuns = 3072;
constexpr uint32_t kRunText = 64 * 1024;
constexpr int kMaxMarks = 512;
constexpr uint32_t kParaBytes = 32 * 1024;
constexpr int kMaxStack = 2048;
constexpr uint32_t kNoObject = 0xFFFFFFFF;

struct Glyph {
  const char* name;
  uint16_t cp;
};
constexpr Glyph kGlyphs[] = {
    {"AE", 0x00C6},
    {"Aacute", 0x00C1},
    {"Abreve", 0x0102},
    {"Acircumflex", 0x00C2},
    {"Adieresis", 0x00C4},
    {"Agrave", 0x00C0},
    {"Amacron", 0x0100},
    {"Aogonek", 0x0104},
    {"Aring", 0x00C5},
    {"Atilde", 0x00C3},
    {"Cacute", 0x0106},
    {"Ccaron", 0x010C},
    {"Ccedilla", 0x00C7},
    {"Ccircumflex", 0x0108},
    {"Cdotaccent", 0x010A},
    {"Dcaron", 0x010E},
    {"Dcroat", 0x0110},
    {"Eacute", 0x00C9},
    {"Ebreve", 0x0114},
    {"Ecaron", 0x011A},
    {"Ecircumflex", 0x00CA},
    {"Edieresis", 0x00CB},
    {"Edotaccent", 0x0116},
    {"Egrave", 0x00C8},
    {"Emacron", 0x0112},
    {"Eogonek", 0x0118},
    {"Eth", 0x00D0},
    {"Euro", 0x20AC},
    {"Gbreve", 0x011E},
    {"Gcedilla", 0x0122},
    {"Gcircumflex", 0x011C},
    {"Gdotaccent", 0x0120},
    {"Hcircumflex", 0x0124},
    {"Hslash", 0x0126},
    {"Iacute", 0x00CD},
    {"Ibreve", 0x012C},
    {"Icircumflex", 0x00CE},
    {"Idieresis", 0x00CF},
    {"Idotaccent", 0x0130},
    {"Igrave", 0x00CC},
    {"Imacron", 0x012A},
    {"Iogonek", 0x012E},
    {"Itilde", 0x0128},
    {"Jcircumflex", 0x0134},
    {"Kcedilla", 0x0136},
    {"Lacute", 0x0139},
    {"Lcaron", 0x013D},
    {"Lcedilla", 0x013B},
    {"Lslash", 0x0141},
    {"Nacute", 0x0143},
    {"Ncaron", 0x0147},
    {"Ncedilla", 0x0145},
    {"Ntilde", 0x00D1},
    {"OE", 0x0152},
    {"Oacute", 0x00D3},
    {"Obreve", 0x014E},
    {"Ocircumflex", 0x00D4},
    {"Odieresis", 0x00D6},
    {"Ograve", 0x00D2},
    {"Ohungarumlaut", 0x0150},
    {"Omacron", 0x014C},
    {"Oslash", 0x00D8},
    {"Otilde", 0x00D5},
    {"Racute", 0x0154},
    {"Rcaron", 0x0158},
    {"Rcedilla", 0x0156},
    {"Sacute", 0x015A},
    {"Scaron", 0x0160},
    {"Scedilla", 0x015E},
    {"Scircumflex", 0x015C},
    {"Tcaron", 0x0164},
    {"Tcedilla", 0x0162},
    {"Thorn", 0x00DE},
    {"Tslash", 0x0166},
    {"Uacute", 0x00DA},
    {"Ubreve", 0x016C},
    {"Ucircumflex", 0x00DB},
    {"Udieresis", 0x00DC},
    {"Ugrave", 0x00D9},
    {"Uhungarumlaut", 0x0170},
    {"Umacron", 0x016A},
    {"Uogonek", 0x0172},
    {"Uring", 0x016E},
    {"Utilde", 0x0168},
    {"Wcircumflex", 0x0174},
    {"Yacute", 0x00DD},
    {"Ycircumflex", 0x0176},
    {"Ydieresis", 0x0178},
    {"Zacute", 0x0179},
    {"Zcaron", 0x017D},
    {"Zdotaccent", 0x017B},
    {"aacute", 0x00E1},
    {"abreve", 0x0103},
    {"acircumflex", 0x00E2},
    {"acute", 0x00B4},
    {"adieresis", 0x00E4},
    {"ae", 0x00E6},
    {"afii61352", 0x2116},
    {"agrave", 0x00E0},
    {"amacron", 0x0101},
    {"ampersand", 0x0026},
    {"aogonek", 0x0105},
    {"aring", 0x00E5},
    {"asciicircum", 0x005E},
    {"asciitilde", 0x007E},
    {"asterisk", 0x002A},
    {"at", 0x0040},
    {"atilde", 0x00E3},
    {"backslash", 0x005C},
    {"bar", 0x007C},
    {"braceleft", 0x007B},
    {"braceright", 0x007D},
    {"bracketleft", 0x005B},
    {"bracketright", 0x005D},
    {"breve", 0x02D8},
    {"brokenbar", 0x00A6},
    {"bullet", 0x2022},
    {"cacute", 0x0107},
    {"caron", 0x02C7},
    {"ccaron", 0x010D},
    {"ccedilla", 0x00E7},
    {"ccircumflex", 0x0109},
    {"cdotaccent", 0x010B},
    {"cedilla", 0x00B8},
    {"cent", 0x00A2},
    {"circumflex", 0x02C6},
    {"colon", 0x003A},
    {"comma", 0x002C},
    {"copyright", 0x00A9},
    {"currency", 0x00A4},
    {"dagger", 0x2020},
    {"daggerdbl", 0x2021},
    {"dcaron", 0x010F},
    {"dcroat", 0x0111},
    {"degree", 0x00B0},
    {"dieresis", 0x00A8},
    {"divide", 0x00F7},
    {"dollar", 0x0024},
    {"dotaccent", 0x02D9},
    {"dotlessi", 0x0131},
    {"eacute", 0x00E9},
    {"ebreve", 0x0115},
    {"ecaron", 0x011B},
    {"ecircumflex", 0x00EA},
    {"edieresis", 0x00EB},
    {"edotaccent", 0x0117},
    {"egrave", 0x00E8},
    {"eight", 0x0038},
    {"ellipsis", 0x2026},
    {"emacron", 0x0113},
    {"emdash", 0x2014},
    {"endash", 0x2013},
    {"eogonek", 0x0119},
    {"equal", 0x003D},
    {"eth", 0x00F0},
    {"exclam", 0x0021},
    {"exclamdown", 0x00A1},
    {"ff", 0xFB00},
    {"ffi", 0xFB03},
    {"ffl", 0xFB04},
    {"fi", 0xFB01},
    {"figuredash", 0x2012},
    {"five", 0x0035},
    {"fl", 0xFB02},
    {"florin", 0x0192},
    {"four", 0x0034},
    {"fraction", 0x2044},
    {"gbreve", 0x011F},
    {"gcedilla", 0x0123},
    {"gcircumflex", 0x011D},
    {"gdotaccent", 0x0121},
    {"germandbls", 0x00DF},
    {"grave", 0x0060},
    {"greater", 0x003E},
    {"guillemetleft", 0x00AB},
    {"guillemetright", 0x00BB},
    {"guillemotleft", 0x00AB},
    {"guillemotright", 0x00BB},
    {"guilsinglleft", 0x2039},
    {"guilsinglright", 0x203A},
    {"hcircumflex", 0x0125},
    {"hslash", 0x0127},
    {"hungarumlaut", 0x02DD},
    {"hyphen", 0x002D},
    {"iacute", 0x00ED},
    {"ibreve", 0x012D},
    {"icircumflex", 0x00EE},
    {"idieresis", 0x00EF},
    {"igrave", 0x00EC},
    {"imacron", 0x012B},
    {"iogonek", 0x012F},
    {"itilde", 0x0129},
    {"jcircumflex", 0x0135},
    {"kcedilla", 0x0137},
    {"lacute", 0x013A},
    {"lcaron", 0x013E},
    {"lcedilla", 0x013C},
    {"less", 0x003C},
    {"logicalnot", 0x00AC},
    {"lslash", 0x0142},
    {"macron", 0x00AF},
    {"middot", 0x00B7},
    {"minus", 0x2212},
    {"mu", 0x00B5},
    {"multiply", 0x00D7},
    {"nacute", 0x0144},
    {"nbspace", 0x00A0},
    {"ncaron", 0x0148},
    {"ncedilla", 0x0146},
    {"nine", 0x0039},
    {"nonbreakingspace", 0x00A0},
    {"ntilde", 0x00F1},
    {"numbersign", 0x0023},
    {"numero", 0x2116},
    {"oacute", 0x00F3},
    {"obreve", 0x014F},
    {"ocircumflex", 0x00F4},
    {"odieresis", 0x00F6},
    {"oe", 0x0153},
    {"ogonek", 0x02DB},
    {"ograve", 0x00F2},
    {"ohungarumlaut", 0x0151},
    {"omacron", 0x014D},
    {"one", 0x0031},
    {"onedotenleader", 0x2024},
    {"onehalf", 0x00BD},
    {"onequarter", 0x00BC},
    {"onesuperior", 0x00B9},
    {"ordfeminine", 0x00AA},
    {"ordmasculine", 0x00BA},
    {"oslash", 0x00F8},
    {"otilde", 0x00F5},
    {"paragraph", 0x00B6},
    {"parenleft", 0x0028},
    {"parenright", 0x0029},
    {"percent", 0x0025},
    {"period", 0x002E},
    {"periodcentered", 0x00B7},
    {"perthousand", 0x2030},
    {"plus", 0x002B},
    {"plusminus", 0x00B1},
    {"question", 0x003F},
    {"questiondown", 0x00BF},
    {"quotedbl", 0x0022},
    {"quotedblbase", 0x201E},
    {"quotedblleft", 0x201C},
    {"quotedblright", 0x201D},
    {"quoteleft", 0x2018},
    {"quoteleftreversed", 0x201B},
    {"quoteright", 0x2019},
    {"quotesinglbase", 0x201A},
    {"quotesingle", 0x0027},
    {"racute", 0x0155},
    {"rcaron", 0x0159},
    {"rcedilla", 0x0157},
    {"registered", 0x00AE},
    {"ring", 0x02DA},
    {"sacute", 0x015B},
    {"scaron", 0x0161},
    {"scedilla", 0x015F},
    {"scircumflex", 0x015D},
    {"section", 0x00A7},
    {"semicolon", 0x003B},
    {"seven", 0x0037},
    {"sfthyphen", 0x00AD},
    {"six", 0x0036},
    {"slash", 0x002F},
    {"softhyphen", 0x00AD},
    {"space", 0x0020},
    {"sterling", 0x00A3},
    {"tcaron", 0x0165},
    {"tcedilla", 0x0163},
    {"thorn", 0x00FE},
    {"three", 0x0033},
    {"threequarters", 0x00BE},
    {"threesuperior", 0x00B3},
    {"tilde", 0x02DC},
    {"trademark", 0x2122},
    {"tslash", 0x0167},
    {"two", 0x0032},
    {"twodotenleader", 0x2025},
    {"twosuperior", 0x00B2},
    {"uacute", 0x00FA},
    {"ubreve", 0x016D},
    {"ucircumflex", 0x00FB},
    {"udieresis", 0x00FC},
    {"ugrave", 0x00F9},
    {"uhungarumlaut", 0x0171},
    {"umacron", 0x016B},
    {"underscore", 0x005F},
    {"uni00A0", 0x00A0},
    {"uogonek", 0x0173},
    {"uring", 0x016F},
    {"utilde", 0x0169},
    {"wcircumflex", 0x0175},
    {"yacute", 0x00FD},
    {"ycircumflex", 0x0177},
    {"ydieresis", 0x00FF},
    {"yen", 0x00A5},
    {"zacute", 0x017A},
    {"zcaron", 0x017E},
    {"zdotaccent", 0x017C},
    {"zero", 0x0030},
};
constexpr uint16_t kHelvetica[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333,  278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667,
    611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667,  611, 722, 667, 944, 667,
    667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278,  556, 556, 222, 222, 500,
    222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500,  500, 334, 260, 334, 584,
};
constexpr uint16_t kHelveticaBold[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667,
    611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
    667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556,
    278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584,
};
constexpr uint16_t kTimes[95] = {
    250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444, 921, 722, 667, 667, 722, 611,
    556, 722, 722, 333, 389, 722, 611, 889, 722, 722, 556, 722, 667, 556, 611, 722, 722, 944, 722,
    722, 611, 333, 278, 333, 469, 500, 333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500,
    278, 778, 500, 500, 500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541,
};
constexpr uint16_t kTimesBold[95] = {
    250, 333, 555, 500, 500, 1000, 833, 278, 333, 333, 500, 570, 250, 333, 250, 278, 500, 500,  500,
    500, 500, 500, 500, 500, 500,  500, 333, 333, 570, 570, 570, 500, 930, 722, 667, 722, 722,  667,
    611, 778, 778, 389, 500, 778,  667, 944, 722, 778, 611, 778, 722, 556, 667, 722, 722, 1000, 722,
    722, 667, 333, 278, 333, 581,  500, 333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333,  556,
    278, 833, 556, 500, 556, 556,  444, 389, 333, 556, 500, 722, 500, 500, 444, 394, 220, 394,  520,
};

// The code point of a glyph name: from the table, "uniXXXX", "uXXXX[XX]",
// single letters, and names with a ".suffix" (small capitals, old-style
// figures) or "_" between ligature parts (the first part is taken).
uint32_t glyph_code_point(std::string_view name) {
  if (const size_t dot = name.find('.'); dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
  auto hex = [](std::string_view h, uint32_t& out) {
    if (h.empty() || h.size() > 6) return false;
    out = 0;
    for (char c : h) {
      const int v = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                           : -1;
      if (v < 0) return false;
      out = out << 4 | static_cast<uint32_t>(v);
    }
    return true;
  };
  uint32_t cp;
  if (name.size() == 7 && name.starts_with("uni") && hex(name.substr(3), cp)) return cp;
  if (name.size() >= 5 && name.size() <= 7 && name[0] == 'u' && hex(name.substr(1), cp)) return cp;
  if (name.size() == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z'))) {
    return static_cast<uint8_t>(name[0]);
  }
  size_t lo = 0, hi = sizeof(kGlyphs) / sizeof(kGlyphs[0]);
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const std::string_view m(kGlyphs[mid].name);
    if (m == name) return kGlyphs[mid].cp;
    if (m < name) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (const size_t us = name.find('_'); us != std::string_view::npos && us > 0)
    return glyph_code_point(name.substr(0, us));
  return 0;
}

bool is_ws(uint8_t c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
bool is_delim(uint8_t c) {
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
         c == '%';
}

enum class Tok : uint8_t { End, Number, Name, String, Hex, ArrayOpen, ArrayClose, DictOpen, DictClose, Word };

struct Token {
  Tok kind = Tok::End;
  std::string_view text;  // a name without '/', a string without its brackets
  double num = 0;
};

// PDF's tokens over text in memory.
class Lex {
 public:
  explicit Lex(std::string_view s, size_t at = 0) : s_(s), i_(at) {}
  size_t pos() const { return i_; }
  void set_pos(size_t p) { i_ = p; }
  bool at_end() {
    ws();
    return i_ >= s_.size();
  }

  void ws() {
    while (i_ < s_.size()) {
      const auto c = static_cast<uint8_t>(s_[i_]);
      if (is_ws(c)) {
        ++i_;
      } else if (c == '%') {
        while (i_ < s_.size() && s_[i_] != '\n' && s_[i_] != '\r') ++i_;
      } else {
        break;
      }
    }
  }

  Token next() {
    ws();
    Token t;
    const size_t n = s_.size();
    if (i_ >= n) return t;
    const char c = s_[i_];
    const size_t start = i_;
    if (c == '/') {
      ++i_;
      while (i_ < n && !is_ws(static_cast<uint8_t>(s_[i_])) && !is_delim(static_cast<uint8_t>(s_[i_]))) ++i_;
      t.kind = Tok::Name;
      t.text = s_.substr(start + 1, i_ - start - 1);
      return t;
    }
    if (c == '(') {
      int depth = 1;
      ++i_;
      while (i_ < n) {
        const char d = s_[i_];
        if (d == '\\') {
          i_ += 2;
          continue;
        }
        if (d == '(') ++depth;
        if (d == ')' && --depth == 0) break;
        ++i_;
      }
      const size_t end = std::min(i_, n);
      t.kind = Tok::String;
      t.text = s_.substr(start + 1, end - start - 1);
      i_ = std::min(end + 1, n);
      return t;
    }
    if (c == '<') {
      if (i_ + 1 < n && s_[i_ + 1] == '<') {
        i_ += 2;
        t.kind = Tok::DictOpen;
        return t;
      }
      const size_t gt = s_.find('>', i_);
      const size_t end = gt == std::string_view::npos ? n : gt;
      t.kind = Tok::Hex;
      t.text = s_.substr(start + 1, end - start - 1);
      i_ = std::min(end + 1, n);
      return t;
    }
    if (c == '>') {
      if (i_ + 1 < n && s_[i_ + 1] == '>') {
        i_ += 2;
        t.kind = Tok::DictClose;
        return t;
      }
      ++i_;
      t.kind = Tok::Word;
      t.text = s_.substr(start, 1);
      return t;
    }
    if (c == '[' || c == ']') {
      ++i_;
      t.kind = c == '[' ? Tok::ArrayOpen : Tok::ArrayClose;
      return t;
    }
    if (c == '{' || c == '}' || c == ')') {
      ++i_;
      t.kind = Tok::Word;
      t.text = s_.substr(start, 1);
      return t;
    }
    if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
      size_t k = i_;
      bool neg = false;
      if (s_[k] == '+' || s_[k] == '-') neg = s_[k++] == '-';
      double v = 0, scale = 0;
      bool digits = false;
      for (; k < n; ++k) {
        const char d = s_[k];
        if (d >= '0' && d <= '9') {
          digits = true;
          if (scale > 0) {
            scale /= 10;
            v += (d - '0') * scale;
          } else {
            v = v * 10 + (d - '0');
          }
        } else if (d == '.' && scale == 0) {
          scale = 1;
        } else {
          break;
        }
      }
      if (digits) {
        i_ = k;
        t.kind = Tok::Number;
        t.num = neg ? -v : v;
        t.text = s_.substr(start, k - start);
        return t;
      }
    }
    while (i_ < n && !is_ws(static_cast<uint8_t>(s_[i_])) && !is_delim(static_cast<uint8_t>(s_[i_]))) ++i_;
    if (i_ == start) ++i_;
    t.kind = Tok::Word;
    t.text = s_.substr(start, i_ - start);
    return t;
  }

  // Skips one value and returns its text; "12 0 R" is one value.
  std::string_view value(int depth = 0) {
    ws();
    const size_t start = i_;
    const Token t = next();
    if (t.kind == Tok::ArrayOpen && depth < 32) {
      for (;;) {
        ws();
        if (i_ >= s_.size()) break;
        if (s_[i_] == ']') {
          ++i_;
          break;
        }
        const size_t before = i_;
        value(depth + 1);
        if (i_ == before) ++i_;
      }
    } else if (t.kind == Tok::DictOpen && depth < 32) {
      for (;;) {
        const Token k = next();
        if (k.kind == Tok::DictClose || k.kind == Tok::End) break;
        if (k.kind != Tok::Name) continue;
        value(depth + 1);
      }
    } else if (t.kind == Tok::Number) {
      const size_t p = i_;
      const Token a = next();
      if (a.kind == Tok::Number) {
        const Token b = next();
        if (b.kind == Tok::Word && b.text == "R") return s_.substr(start, i_ - start);
      }
      i_ = p;
    }
    return s_.substr(start, i_ - start);
  }

 private:
  std::string_view s_;
  size_t i_;
};

// The value of `key` in the dictionary text `dict` (top level only), or empty.
std::string_view dict_get(std::string_view dict, std::string_view key) {
  Lex l(dict);
  if (l.next().kind != Tok::DictOpen) return {};
  for (;;) {
    const Token k = l.next();
    if (k.kind == Tok::DictClose || k.kind == Tok::End) return {};
    if (k.kind != Tok::Name) continue;
    const std::string_view v = l.value();
    if (k.text == key) return v;
  }
}

bool ref_of(std::string_view v, uint32_t& num) {
  Lex l(v);
  const Token a = l.next(), b = l.next(), r = l.next();
  if (a.kind != Tok::Number || b.kind != Tok::Number || r.kind != Tok::Word || r.text != "R" || a.num < 0) {
    return false;
  }
  num = static_cast<uint32_t>(a.num);
  return true;
}

double num_of(std::string_view v, double fallback) {
  Lex l(v);
  const Token t = l.next();
  return t.kind == Tok::Number ? t.num : fallback;
}

std::string_view name_of(std::string_view v) {
  Lex l(v);
  const Token t = l.next();
  return t.kind == Tok::Name ? t.text : std::string_view{};
}

// Calls fn(value) for each item of an array's text.
template <typename Fn>
void for_items(std::string_view array, Fn&& fn) {
  Lex l(array);
  if (l.next().kind != Tok::ArrayOpen) return;
  for (;;) {
    l.ws();
    if (l.at_end()) return;
    const size_t p = l.pos();
    if (array[p] == ']') return;
    const std::string_view v = l.value();
    if (l.pos() == p) return;
    fn(v);
  }
}

int hex_digit(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                : -1;
}

// The bytes of a string token: literal escapes or hex digits decoded.
size_t string_bytes(const Token& t, uint8_t* out, size_t cap) {
  size_t w = 0;
  const std::string_view s = t.text;
  if (t.kind == Tok::Hex) {
    int hi = -1;
    for (char c : s) {
      const int v = hex_digit(c);
      if (v < 0) continue;
      if (hi < 0) {
        hi = v;
      } else {
        if (w < cap) out[w++] = static_cast<uint8_t>(hi << 4 | v);
        hi = -1;
      }
    }
    if (hi >= 0 && w < cap) out[w++] = static_cast<uint8_t>(hi << 4);
    return w;
  }
  for (size_t i = 0; i < s.size() && w < cap; ++i) {
    char c = s[i];
    if (c == '\r') {
      out[w++] = '\n';
      if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
      continue;
    }
    if (c != '\\' || i + 1 >= s.size()) {
      out[w++] = static_cast<uint8_t>(c);
      continue;
    }
    c = s[++i];
    switch (c) {
      case 'n':
        out[w++] = '\n';
        break;
      case 'r':
        out[w++] = '\r';
        break;
      case 't':
        out[w++] = '\t';
        break;
      case 'b':
        out[w++] = '\b';
        break;
      case 'f':
        out[w++] = '\f';
        break;
      case '\r':
        if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        break;
      case '\n':
        break;
      default:
        if (c >= '0' && c <= '7') {
          int v = c - '0';
          for (int k = 0; k < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7'; ++k)
            v = v * 8 + (s[++i] - '0');
          out[w++] = static_cast<uint8_t>(v);
        } else {
          out[w++] = static_cast<uint8_t>(c);
        }
    }
  }
  return w;
}

// A text string (titles, outline entries) as UTF-8: UTF-16BE with its
// byte order mark, UTF-8 with its own, else PDFDocEncoding (much like
// Windows-1252).
size_t text_string(const Token& t, char* out, size_t cap) {
  uint8_t raw[512];
  const size_t n = string_bytes(t, raw, sizeof(raw));
  size_t w = 0;
  if (n >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
    for (size_t i = 2; i + 1 < n && w + 5 < cap; i += 2) {
      uint32_t cp = static_cast<uint32_t>(raw[i] << 8 | raw[i + 1]);
      if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < n) {
        const uint32_t lo = static_cast<uint32_t>(raw[i + 2] << 8 | raw[i + 3]);
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
      if (cp >= 0x20) w += xml::encode_utf8(cp, out + w);
    }
  } else if (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
    for (size_t i = 3; i < n && w + 1 < cap; ++i) out[w++] = static_cast<char>(raw[i]);
  } else {
    for (size_t i = 0; i < n && w + 5 < cap; ++i) {
      if (raw[i] >= 0x20) w += xml::encode_utf8(charset::code_point(charset::Encoding::Windows1252, raw[i]), out + w);
    }
  }
  out[w] = '\0';
  return w;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_ws(static_cast<uint8_t>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && is_ws(static_cast<uint8_t>(s.back()))) s.remove_suffix(1);
  return s;
}

// ---- Fonts -------------------------------------------------------------------------

// Where a font's characters come from, kept in a shared pool of words.
struct Font {
  uint32_t obj = kNoObject;
  bool two_byte = false;  // a composite (Type0) font: two-byte codes
  uint8_t style = 0;      // StyleRun-like: 1 bold, 2 italic
  // One-byte fonts: 256 code points (0 unknown; bit 31: a sequence in the
  // pool) and 256 widths. Two-byte fonts: sorted ranges.
  uint32_t map = 0;
  uint32_t widths = 0;
  uint32_t ranges = 0;  // (lo << 16 | hi, first code point or sequence) pairs
  uint32_t range_count = 0;
  uint32_t wranges = 0;  // (lo << 16 | hi, width) pairs
  uint32_t wrange_count = 0;
  uint16_t default_width = 500;
  bool has_widths = false;
};

constexpr uint32_t kSequence = 0x80000000u;

// ---- Matrices ----------------------------------------------------------------------

struct Matrix {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  // this × m
  Matrix times(const Matrix& m) const {
    return {a * m.a + b * m.c, a * m.b + b * m.d,       c * m.a + d * m.c,
            c * m.b + d * m.d, e * m.a + f * m.c + m.e, e * m.b + f * m.d + m.f};
  }
};

// ---- Layout ------------------------------------------------------------------------

struct Run {
  float x0, x1, y, size;
  uint32_t text;  // in the page's text pool
  uint16_t len;
  uint8_t style;
};

struct Line {
  float x0, x1, y, size;
  uint16_t first, count;  // runs
  uint32_t chars;
  bool drop;
  bool title = false;  // says a contents entry's title
};

struct Outline {
  uint32_t page;
  uint8_t depth;
  char title[96];
};

bool sentence_end(uint32_t cp) {
  return cp == '.' || cp == '!' || cp == '?' || cp == ':' || cp == '"' || cp == '\'' || cp == 0x201D || cp == 0x2019 ||
         cp == 0xBB || cp == ')' || cp == 0x2026 || cp == ';';
}

class Pdf {
 public:
  explicit Pdf(Job& job) : job_(job), f_(*job.in) {
    uint8_t* p = job.work;
    auto take = [&](uint32_t n) {
      uint8_t* at = p;
      p += (n + 15) / 16 * 16;
      return at;
    };
    xref_ = reinterpret_cast<XRef*>(take(kMaxObjects * sizeof(XRef)));
    pages_ = reinterpret_cast<PageRef*>(take(kMaxPages * sizeof(PageRef)));
    for (int i = 0; i < kSlots; ++i) slot_[i] = reinterpret_cast<char*>(take(kSlotBytes));
    objstm_ = take(kObjStmBytes);
    content_ = take(kContentBytes);
    chunk_ = take(kChunk);
    inflater_ = new (take(sizeof(tinfl_decompressor))) tinfl_decompressor;
    pool_ = reinterpret_cast<uint32_t*>(take(kPoolWords * 4));
    fonts_ = reinterpret_cast<Font*>(take(kMaxFonts * sizeof(Font)));
    runs_ = reinterpret_cast<Run*>(take(kMaxRuns * sizeof(Run)));
    lines_ = reinterpret_cast<Line*>(take(kMaxRuns * sizeof(Line)));
    run_text_ = reinterpret_cast<char*>(take(kRunText));
    outline_ = reinterpret_cast<Outline*>(take(kMaxMarks * sizeof(Outline)));
    para_ = reinterpret_cast<char*>(take(kParaBytes));
    stack_ = reinterpret_cast<StackItem*>(take(kMaxStack * sizeof(StackItem)));
    ok_ = p <= job.work + job.work_size;
    static_assert(kMaxObjects * sizeof(XRef) + kMaxPages * sizeof(PageRef) + kSlots * kSlotBytes + kObjStmBytes +
                      kContentBytes + kChunk + sizeof(tinfl_decompressor) + kPoolWords * 4 + kMaxFonts * sizeof(Font) +
                      kMaxRuns * (sizeof(Run) + sizeof(Line)) + kRunText + kMaxMarks * sizeof(Outline) + kParaBytes +
                      kMaxStack * sizeof(StackItem) + 16 * 16 <=
                  kWorkBytes);
  }

  Result run() {
    if (!ok_) return Result::NoMemory;
    char head[8] = {};
    if (f_.read_at(0, reinterpret_cast<uint8_t*>(head), 5) != 5 || std::memcmp(head, "%PDF-", 5) != 0) {
      // Some files have a few bytes before the header.
      uint8_t probe[1024];
      const uint32_t n = std::min<uint32_t>(f_.size(), sizeof(probe));
      if (f_.read_at(0, probe, n) != static_cast<int32_t>(n) ||
          std::string_view(reinterpret_cast<char*>(probe), n).find("%PDF-") == std::string_view::npos) {
        return Result::Unreadable;
      }
    }
    std::memset(xref_, 0, kMaxObjects * sizeof(XRef));
    if (!read_xref() || root_ == kNoObject) {
      std::memset(xref_, 0, kMaxObjects * sizeof(XRef));
      root_ = info_ = kNoObject;
      encrypted_ = false;
      if (!rebuild_xref() || root_ == kNoObject) return Result::Unreadable;
    }
    if (encrypted_) return Result::Protected;
    if (!collect_pages() || page_count_ == 0) return Result::Unreadable;
    read_outline();

    char title[160] = "", author[160] = "", language[16] = "";
    read_info(title, author, language);
    Page page(job_);
    if (!page.ok()) return Result::Unreadable;
    page_ = &page;
    page.start(title, author, language, "", outline_count_ > 0);
    // The first page's running head and foot are known by the next two.
    probe_ = true;
    for (uint32_t i = 2; i >= 1; --i) {
      if (i < page_count_) read_page(i);
    }
    probe_ = false;
    for (uint32_t i = 0; i < page_count_; ++i) {
      // Contents entries name a page; each is placed at its heading there.
      while (mark_end_ < outline_count_ && outline_[mark_end_].page <= i) ++mark_end_;
      read_page(i);
      if (!page.ok()) break;
    }
    end_paragraph();
    mark_end_ = outline_count_;
    flush_marks(-2);
    page.end();
    page_ = nullptr;
    if (!page.ok()) return Result::Unreadable;
    // Pictures of pages, with next to no text: a scan.
    if (chars_ < 64 + 8 * page_count_ / 10) return Result::NoText;
    return page.finish(true) ? Result::Ok : Result::Unreadable;
  }

 private:
  struct XRef {
    uint32_t at;  // offset, or the object stream's number
    uint16_t index;
    uint8_t type;  // 0 free or unknown, 1 at an offset, 2 in an object stream
    uint8_t pad;
  };
  struct PageRef {
    uint32_t obj;
    uint32_t res;  // the object whose /Resources applies
    float y0, y1;  // its media box, bottom and top
  };
  struct StackItem {
    uint32_t obj;
    uint32_t res;
    float y0, y1;
    uint8_t depth;
  };
  struct Obj {
    std::string_view text;  // the value (for a stream, its dictionary)
    bool stream = false;
    uint32_t stream_at = 0;
    uint32_t stream_len = 0;
  };

  // ---- Reading objects ----

  // Inflates the stream of `o` into out; the bytes written, or -1. Only
  // FlateDecode (with PNG predictors) is known; an unfiltered stream is
  // copied.
  int32_t stream_data(const Obj& o, uint8_t* out, uint32_t cap) {
    if (!o.stream) return -1;
    // Filters: Flate, maybe after an ASCII one (as ReportLab writes them).
    const std::string_view filter = dict_get(o.text, "Filter");
    std::string_view names[2];
    int count = 0;
    if (!name_of(filter).empty()) {
      names[count++] = name_of(filter);
    } else if (!filter.empty()) {
      bool fits = true;
      for_items(filter, [&](std::string_view v) {
        if (count < 2) {
          names[count++] = name_of(v);
        } else {
          fits = false;
        }
      });
      if (!fits) return -1;
    }
    Source src{o.stream_at, o.stream_at + o.stream_len};
    bool flate = false;
    for (int i = 0; i < count; ++i) {
      const std::string_view n = names[i];
      if ((n == "FlateDecode" || n == "Fl") && i == count - 1) {
        flate = true;
      } else if ((n == "ASCII85Decode" || n == "A85") && i == 0) {
        src.ascii = Source::kAscii85;
      } else if ((n == "ASCIIHexDecode" || n == "AHx") && i == 0) {
        src.ascii = Source::kAsciiHex;
      } else {
        return -1;
      }
    }
    int32_t n;
    if (flate) {
      n = inflate(src, out, cap);
    } else {
      n = 0;
      while (static_cast<uint32_t>(n) < cap) {
        const int32_t got = pull(src, out + n, cap - static_cast<uint32_t>(n));
        if (got <= 0) break;
        n += got;
      }
      if (n == 0) n = -1;
    }
    if (n <= 0) return n;
    std::string_view parms = dict_get(o.text, "DecodeParms");
    if (parms.empty()) parms = dict_get(o.text, "DP");
    if (!parms.empty() && parms[0] == '[') {
      std::string_view first;
      for_items(parms, [&](std::string_view v) {
        if (first.empty()) first = v;
      });
      parms = first;
    }
    const int predictor = static_cast<int>(num_of(dict_get(parms, "Predictor"), 1));
    if (predictor >= 10) {
      const int columns = static_cast<int>(num_of(dict_get(parms, "Columns"), 1));
      const int colors = static_cast<int>(num_of(dict_get(parms, "Colors"), 1));
      const int bpc = static_cast<int>(num_of(dict_get(parms, "BitsPerComponent"), 8));
      n = unpredict(out, static_cast<uint32_t>(n), columns, colors, bpc);
    }
    return n;
  }

  // Undoes PNG row filters in place; returns the bytes left.
  static int32_t unpredict(uint8_t* d, uint32_t n, int columns, int colors, int bpc) {
    const uint32_t bpp = std::max(1, colors * bpc / 8);
    const uint32_t row = static_cast<uint32_t>((columns * colors * bpc + 7) / 8);
    if (row == 0) return 0;
    uint32_t w = 0;
    for (uint32_t r = 0; r + row + 1 <= n; r += row + 1) {
      const uint8_t type = d[r];
      uint8_t* cur = d + w;
      const uint8_t* prev = w >= row ? d + w - row : nullptr;
      std::memmove(cur, d + r + 1, row);
      for (uint32_t x = 0; x < row; ++x) {
        const int left = x >= bpp ? cur[x - bpp] : 0;
        const int up = prev != nullptr ? prev[x] : 0;
        const int ul = prev != nullptr && x >= bpp ? prev[x - bpp] : 0;
        int add = 0;
        switch (type) {
          case 1:
            add = left;
            break;
          case 2:
            add = up;
            break;
          case 3:
            add = (left + up) / 2;
            break;
          case 4: {
            const int p = left + up - ul;
            const int pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - ul);
            add = pa <= pb && pa <= pc ? left : pb <= pc ? up : ul;
            break;
          }
          default:
            break;
        }
        cur[x] = static_cast<uint8_t>(cur[x] + add);
      }
      w += row;
    }
    return static_cast<int32_t>(w);
  }

  // A stream's bytes from the file, with an ASCII filter undone on the way.
  struct Source {
    static constexpr uint8_t kAscii85 = 1, kAsciiHex = 2;
    uint32_t at, end;
    uint8_t ascii = 0;
    bool done = false;
    uint32_t tuple = 0;  // ASCII85: the group so far
    int digits = 0;      // its digits, or a hex digit's half
  };

  // Up to cap bytes into out; 0 at the end, -1 on a read error.
  int32_t pull(Source& s, uint8_t* out, uint32_t cap) {
    if (s.done || cap == 0) return 0;
    if (s.ascii == 0) {
      const uint32_t want = std::min(s.end - s.at, cap);
      if (want == 0 || f_.read_at(s.at, out, want) != static_cast<int32_t>(want)) {
        s.done = true;
        return want == 0 ? 0 : -1;
      }
      s.at += want;
      if (s.at >= s.end) s.done = true;
      return static_cast<int32_t>(want);
    }
    uint32_t n = 0;
    uint8_t raw[1024];
    // Room for one more group's bytes, so a read never leaves any behind.
    while (!s.done && n + 4 <= cap) {
      const uint32_t want = std::min<uint32_t>(s.end - s.at, sizeof(raw));
      if (want == 0 || f_.read_at(s.at, raw, want) != static_cast<int32_t>(want)) {
        s.done = true;
        break;
      }
      uint32_t i = 0;
      for (; i < want && !s.done && n + 4 <= cap; ++i) {
        const uint8_t c = raw[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0) continue;
        if (s.ascii == Source::kAsciiHex) {
          const int v = c >= '0' && c <= '9'                     ? c - '0'
                        : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10
                                                                 : -1;
          if (v < 0) {
            if (s.digits == 1) out[n++] = static_cast<uint8_t>(s.tuple << 4);
            s.done = true;  // '>' or damage: the end
            break;
          }
          s.tuple = (s.tuple << 4) | static_cast<uint32_t>(v);
          if (++s.digits == 2) {
            out[n++] = static_cast<uint8_t>(s.tuple);
            s.tuple = 0;
            s.digits = 0;
          }
          continue;
        }
        if (c == 'z' && s.digits == 0) {
          std::memset(out + n, 0, 4);
          n += 4;
          continue;
        }
        if (c < '!' || c > 'u') {
          // "~>" or damage: a last, short group is padded with 'u'.
          if (s.digits > 1) {
            const int have = s.digits;
            for (int k = have; k < 5; ++k) s.tuple = s.tuple * 85 + 84;
            for (int k = 0; k < have - 1; ++k) out[n++] = static_cast<uint8_t>(s.tuple >> (24 - 8 * k));
          }
          s.done = true;
          break;
        }
        s.tuple = s.tuple * 85 + static_cast<uint32_t>(c - '!');
        if (++s.digits == 5) {
          for (int k = 0; k < 4; ++k) out[n++] = static_cast<uint8_t>(s.tuple >> (24 - 8 * k));
          s.tuple = 0;
          s.digits = 0;
        }
      }
      s.at += i;
      if (s.at >= s.end) s.done = true;
    }
    return static_cast<int32_t>(n);
  }

  int32_t inflate(Source& src, uint8_t* out, uint32_t cap) {
    tinfl_init(inflater_);
    size_t written = 0;
    while (true) {
      const int32_t got = pull(src, chunk_, kChunk);
      if (got < 0) break;
      const auto want = static_cast<uint32_t>(got);
      size_t used = 0;
      for (;;) {
        size_t in_size = want - used, out_size = cap - written;
        const mz_uint32 flags = TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF |
                                (!src.done ? TINFL_FLAG_HAS_MORE_INPUT : 0);
        const tinfl_status st =
            tinfl_decompress(inflater_, chunk_ + used, &in_size, out, out + written, &out_size, flags);
        used += in_size;
        written += out_size;
        if (st == TINFL_STATUS_DONE || st == TINFL_STATUS_ADLER32_MISMATCH || st == TINFL_STATUS_HAS_MORE_OUTPUT) {
          return static_cast<int32_t>(written);  // done, or as much as fits
        }
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT) break;
        return written > 0 ? static_cast<int32_t>(written) : -1;  // damaged: keep what came out
      }
      if (src.done) break;
    }
    return written > 0 ? static_cast<int32_t>(written) : -1;
  }

  // Reads object `num` into slot s.
  bool fetch(uint32_t num, int s, Obj& out, int depth = 0) {
    out = {};
    if (num >= kMaxObjects || depth > 4) return false;
    const XRef& x = xref_[num];
    char* buf = slot_[s];
    if (x.type == 1) {
      const uint32_t size = f_.size();
      if (x.at >= size) return false;
      const uint32_t want = std::min(kSlotBytes, size - x.at);
      if (f_.read_at(x.at, reinterpret_cast<uint8_t*>(buf), want) != static_cast<int32_t>(want)) return false;
      const std::string_view all(buf, want);
      Lex l(all);
      const Token a = l.next(), b = l.next(), o = l.next();
      if (a.kind != Tok::Number || b.kind != Tok::Number || o.text != "obj") return false;
      l.ws();
      const size_t start = l.pos();
      out.text = l.value();
      if (out.text.empty() && start < all.size()) out.text = all.substr(start);
      // A stream follows its dictionary.
      size_t p = start + out.text.size();
      while (p < all.size() && is_ws(static_cast<uint8_t>(all[p]))) ++p;
      if (all.substr(p, 6) == "stream") {
        p += 6;
        if (p < all.size() && all[p] == '\r') ++p;
        if (p < all.size() && all[p] == '\n') ++p;
        out.stream = true;
        out.stream_at = x.at + static_cast<uint32_t>(p);
        const std::string_view lv = dict_get(out.text, "Length");
        uint32_t ref;
        double len = -1;
        if (ref_of(lv, ref)) {
          len = object_number(ref);
        } else {
          len = num_of(lv, -1);
        }
        if (len < 0 || out.stream_at + len > size) {
          len = find_endstream(out.stream_at);
        }
        out.stream_len = static_cast<uint32_t>(std::max(0.0, len));
      }
      return true;
    }
    if (x.type == 2) {
      if (!load_objstm(x.at, depth)) return false;
      // The header: pairs of object number and offset after /First.
      Lex l(std::string_view(reinterpret_cast<char*>(objstm_), objstm_first_));
      uint32_t off = kNoObject, next = kNoObject;
      for (uint32_t k = 0; k < objstm_count_; ++k) {
        const Token n = l.next(), o = l.next();
        if (n.kind != Tok::Number || o.kind != Tok::Number) break;
        if (off != kNoObject) {
          next = static_cast<uint32_t>(o.num);
          break;
        }
        if (static_cast<uint32_t>(n.num) == num && k == x.index) off = static_cast<uint32_t>(o.num);
      }
      if (off == kNoObject) {
        // The index may be wrong: look by number alone.
        Lex m(std::string_view(reinterpret_cast<char*>(objstm_), objstm_first_));
        for (uint32_t k = 0; k < objstm_count_ && off == kNoObject; ++k) {
          const Token n = m.next(), o = m.next();
          if (n.kind != Tok::Number || o.kind != Tok::Number) break;
          if (static_cast<uint32_t>(n.num) == num) {
            off = static_cast<uint32_t>(o.num);
            const Token n2 = m.next(), o2 = m.next();
            if (n2.kind == Tok::Number && o2.kind == Tok::Number) next = static_cast<uint32_t>(o2.num);
          }
        }
      }
      if (off == kNoObject) return false;
      const uint32_t from = objstm_first_ + off;
      uint32_t to = next != kNoObject ? objstm_first_ + next : objstm_len_;
      if (from >= objstm_len_) return false;
      to = std::min(std::max(to, from), objstm_len_);
      const uint32_t n = std::min(to - from, kSlotBytes);
      std::memcpy(buf, objstm_ + from, n);
      Lex l2(std::string_view(buf, n));
      out.text = l2.value();
      return true;
    }
    return false;
  }

  double object_number(uint32_t num) {
    if (num >= kMaxObjects || xref_[num].type != 1) return -1;
    char buf[64];
    const uint32_t size = f_.size();
    const uint32_t at = xref_[num].at;
    if (at >= size) return -1;
    const uint32_t n = std::min<uint32_t>(sizeof(buf), size - at);
    if (f_.read_at(at, reinterpret_cast<uint8_t*>(buf), n) != static_cast<int32_t>(n)) return -1;
    Lex l(std::string_view(buf, n));
    l.next();
    l.next();
    l.next();
    const Token t = l.next();
    return t.kind == Tok::Number ? t.num : -1;
  }

  double find_endstream(uint32_t from) {
    const uint32_t size = f_.size();
    for (uint32_t at = from; at < size;) {
      const uint32_t n = std::min(kChunk, size - at);
      if (f_.read_at(at, chunk_, n) != static_cast<int32_t>(n)) return -1;
      const std::string_view s(reinterpret_cast<char*>(chunk_), n);
      const size_t k = s.find("endstream");
      if (k != std::string_view::npos) {
        size_t e = k;
        while (e > 0 && (s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
        return at + e - from;
      }
      if (at + n >= size) break;
      at += n - 9;
    }
    return -1;
  }

  bool load_objstm(uint32_t num, int depth) {
    if (num == objstm_num_) return true;
    objstm_num_ = kNoObject;
    Obj o;
    // The object stream's own dictionary goes in the last slot.
    if (!fetch(num, kSlots - 1, o, depth + 1) || !o.stream) return false;
    objstm_first_ = static_cast<uint32_t>(num_of(dict_get(o.text, "First"), 0));
    objstm_count_ = static_cast<uint32_t>(num_of(dict_get(o.text, "N"), 0));
    const int32_t n = stream_data(o, objstm_, kObjStmBytes);
    if (n <= 0 || objstm_first_ >= static_cast<uint32_t>(n)) return false;
    objstm_len_ = static_cast<uint32_t>(n);
    objstm_num_ = num;
    return true;
  }

  // The value text of v, following a reference into slot s.
  std::string_view resolve(std::string_view v, int s, Obj* obj = nullptr) {
    uint32_t r;
    if (!ref_of(v, r)) return v;
    Obj o;
    if (!fetch(r, s, o)) return {};
    if (obj != nullptr) *obj = o;
    return o.text;
  }

  // ---- The cross-reference table ----

  bool read_xref() {
    const uint32_t size = f_.size();
    const uint32_t tail = std::min<uint32_t>(size, 2048);
    char buf[2048];
    if (f_.read_at(size - tail, reinterpret_cast<uint8_t*>(buf), tail) != static_cast<int32_t>(tail)) return false;
    const std::string_view s(buf, tail);
    const size_t k = s.rfind("startxref");
    if (k == std::string_view::npos) return false;
    Lex l(s, k + 9);
    const Token t = l.next();
    if (t.kind != Tok::Number) return false;
    uint32_t at = static_cast<uint32_t>(t.num);
    for (int section = 0; section < 64 && at > 0 && at < size; ++section) {
      uint32_t prev = 0;
      if (!read_xref_section(at, prev)) return section > 0;
      at = prev;
    }
    return true;
  }

  // One table or stream at `at`; sets prev to the one before it (0 for none).
  bool read_xref_section(uint32_t at, uint32_t& prev) {
    char* buf = slot_[0];
    const uint32_t size = f_.size();
    const uint32_t want = std::min(kSlotBytes, size - at);
    if (f_.read_at(at, reinterpret_cast<uint8_t*>(buf), want) != static_cast<int32_t>(want)) return false;
    std::string_view s(buf, want);
    size_t p = 0;
    while (p < s.size() && is_ws(static_cast<uint8_t>(s[p]))) ++p;
    if (s.substr(p, 4) == "xref") {
      // A classic table, read as it goes: it can be longer than the slot.
      uint32_t pos = at + static_cast<uint32_t>(p) + 4;
      for (;;) {
        const uint32_t n = std::min(kSlotBytes, size - pos);
        if (n == 0 || f_.read_at(pos, reinterpret_cast<uint8_t*>(buf), n) != static_cast<int32_t>(n)) return false;
        s = std::string_view(buf, n);
        Lex l(s);
        const Token a = l.next();
        if (a.kind == Tok::Word && a.text == "trailer") {
          Lex t(s, l.pos());
          trailer(t.value(), prev);
          return true;
        }
        const Token b = l.next();
        if (a.kind != Tok::Number || b.kind != Tok::Number) return false;
        uint32_t first = static_cast<uint32_t>(a.num);
        const uint32_t count = static_cast<uint32_t>(b.num);
        l.ws();
        size_t q = l.pos();
        for (uint32_t k = 0; k < count; ++k) {
          if (q + 18 > s.size()) {
            // Read on from here.
            pos += static_cast<uint32_t>(q);
            const uint32_t m = std::min(kSlotBytes, size - pos);
            if (m < 18 || f_.read_at(pos, reinterpret_cast<uint8_t*>(buf), m) != static_cast<int32_t>(m)) return false;
            s = std::string_view(buf, m);
            q = 0;
          }
          const uint32_t obj = first + k;
          // "nnnnnnnnnn ggggg n" and an end of line.
          uint32_t off = 0;
          size_t i = q;
          while (i < s.size() && s[i] >= '0' && s[i] <= '9') off = off * 10 + static_cast<uint32_t>(s[i++] - '0');
          while (i < s.size() && s[i] == ' ') ++i;
          while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
          while (i < s.size() && s[i] == ' ') ++i;
          const char kind = i < s.size() ? s[i] : 'f';
          if (obj < kMaxObjects && xref_[obj].type == 0 && kind == 'n' && off > 0) xref_[obj] = {off, 0, 1, 0};
          if (obj < kMaxObjects && xref_[obj].type == 0 && kind == 'f')
            xref_[obj].type = 3;  // freed: older ones stay out
          ++i;
          while (i < s.size() && (s[i] == '\r' || s[i] == '\n' || s[i] == ' ')) ++i;
          q = i;
        }
        pos += static_cast<uint32_t>(q);
      }
    }
    // A cross-reference stream: "n 0 obj << /Type /XRef ... >> stream".
    Lex l(s, p);
    const Token a = l.next(), b = l.next(), o = l.next();
    if (a.kind != Tok::Number || b.kind != Tok::Number || o.text != "obj") return false;
    const uint32_t self = static_cast<uint32_t>(a.num);
    if (self < kMaxObjects && xref_[self].type == 0) xref_[self] = {at + static_cast<uint32_t>(p), 0, 1, 0};
    Obj x;
    if (!fetch(self, 1, x) || !x.stream || name_of(dict_get(x.text, "Type")) != "XRef") return false;
    int w[3] = {0, 0, 0};
    int wi = 0;
    for_items(dict_get(x.text, "W"), [&](std::string_view v) {
      if (wi < 3) w[wi++] = static_cast<int>(num_of(v, 0));
    });
    const uint32_t entry = static_cast<uint32_t>(w[0] + w[1] + w[2]);
    if (wi != 3 || entry == 0 || w[0] > 4 || w[1] > 4 || w[2] > 4) return false;
    const int32_t n = stream_data(x, content_, kContentBytes);
    if (n <= 0) return false;
    uint32_t ranges[128];
    int range_count = 0;
    for_items(dict_get(x.text, "Index"), [&](std::string_view v) {
      if (range_count < 128) ranges[range_count++] = static_cast<uint32_t>(num_of(v, 0));
    });
    if (range_count < 2) {
      ranges[0] = 0;
      ranges[1] = static_cast<uint32_t>(num_of(dict_get(x.text, "Size"), 0));
      range_count = 2;
    }
    const uint8_t* d = content_;
    uint32_t pos = 0;
    auto field = [&](int width, uint32_t fallback) {
      if (width == 0) return fallback;
      uint32_t v = 0;
      for (int k = 0; k < width; ++k) v = v << 8 | d[pos++];
      return v;
    };
    for (int r = 0; r + 1 < range_count; r += 2) {
      for (uint32_t k = 0; k < ranges[r + 1]; ++k) {
        if (pos + entry > static_cast<uint32_t>(n)) break;
        const uint32_t type = field(w[0], 1), f2 = field(w[1], 0), f3 = field(w[2], 0);
        const uint32_t obj = ranges[r] + k;
        if (obj >= kMaxObjects || xref_[obj].type != 0) continue;
        if (type == 1) xref_[obj] = {f2, 0, 1, 0};
        if (type == 2) xref_[obj] = {f2, static_cast<uint16_t>(f3), 2, 0};
        if (type == 0) xref_[obj].type = 3;
      }
    }
    trailer(x.text, prev);
    return true;
  }

  void trailer(std::string_view t, uint32_t& prev) {
    uint32_t r;
    if (root_ == kNoObject && ref_of(dict_get(t, "Root"), r)) root_ = r;
    if (info_ == kNoObject && ref_of(dict_get(t, "Info"), r)) info_ = r;
    if (!dict_get(t, "Encrypt").empty()) encrypted_ = true;
    prev = static_cast<uint32_t>(num_of(dict_get(t, "Prev"), 0));
    // A hybrid file: its stream of compressed objects too.
    const double stm = num_of(dict_get(t, "XRefStm"), 0);
    if (stm > 0 && stm < f_.size()) {
      uint32_t ignored = 0;
      read_xref_section(static_cast<uint32_t>(stm), ignored);
    }
  }

  // A damaged table: find "n g obj" all through the file.
  bool rebuild_xref() {
    const uint32_t size = f_.size();
    uint32_t at = 0;
    while (at < size) {
      const uint32_t n = std::min(kSlotBytes, size - at);
      char* buf = slot_[0];
      if (f_.read_at(at, reinterpret_cast<uint8_t*>(buf), n) != static_cast<int32_t>(n)) return false;
      const std::string_view s(buf, n);
      for (size_t k = s.find(" obj"); k != std::string_view::npos; k = s.find(" obj", k + 4)) {
        // Back over "g" and "n".
        size_t e = k;
        size_t g = e;
        while (g > 0 && s[g - 1] >= '0' && s[g - 1] <= '9') --g;
        if (g == e || g == 0 || s[g - 1] != ' ') continue;
        size_t o = g - 1;
        size_t b = o;
        while (b > 0 && s[b - 1] >= '0' && s[b - 1] <= '9') --b;
        if (b == o) continue;
        uint32_t num = 0;
        for (size_t i = b; i < o; ++i) num = num * 10 + static_cast<uint32_t>(s[i] - '0');
        if (num < kMaxObjects) xref_[num] = {at + static_cast<uint32_t>(b), 0, 1, 0};
      }
      for (size_t k = s.find("trailer"); k != std::string_view::npos; k = s.find("trailer", k + 7)) {
        Lex l(s, k + 7);
        uint32_t ignored;
        const std::string_view t = l.value();
        if (!t.empty() && t[0] == '<') {
          uint32_t r;
          if (ref_of(dict_get(t, "Root"), r)) root_ = r;
          if (ref_of(dict_get(t, "Info"), r)) info_ = r;
          if (!dict_get(t, "Encrypt").empty()) encrypted_ = true;
        }
        (void)ignored;
      }
      if (at + n >= size) break;
      at += n - 32;
    }
    if (root_ == kNoObject) {
      // No trailer: the catalog is the object of /Type /Catalog.
      for (uint32_t k = 1; k < kMaxObjects && root_ == kNoObject; ++k) {
        Obj o;
        if (xref_[k].type == 1 && fetch(k, 0, o) && name_of(dict_get(o.text, "Type")) == "Catalog") root_ = k;
      }
    }
    return true;
  }

  // ---- Pages, outline and details ----

  bool collect_pages() {
    Obj cat;
    if (!fetch(root_, 0, cat)) return false;
    uint32_t pages;
    if (!ref_of(dict_get(cat.text, "Pages"), pages)) return false;
    lang_[0] = '\0';
    const std::string_view lang = dict_get(cat.text, "Lang");
    if (!lang.empty()) {
      Lex l(lang);
      const Token t = l.next();
      char buf[32];
      text_string(t, buf, sizeof(buf));
      size_t k = 0;
      while (buf[k] != '\0' && buf[k] != '-' && buf[k] != '_' && k + 1 < sizeof(lang_)) {
        lang_[k] = ascii_lower(buf[k]);
        ++k;
      }
      lang_[k] = '\0';
    }
    int top = 0;
    stack_[top++] = {pages, kNoObject, 0, 792, 0};
    page_count_ = 0;
    uint32_t visited = 0;
    while (top > 0 && page_count_ < kMaxPages && visited++ < 4 * kMaxPages) {
      const StackItem it = stack_[--top];
      Obj o;
      if (!fetch(it.obj, 0, o)) continue;
      uint32_t res = it.res;
      if (!dict_get(o.text, "Resources").empty()) res = it.obj;
      float y0 = it.y0, y1 = it.y1;
      std::string_view box = dict_get(o.text, "CropBox");
      if (box.empty()) box = dict_get(o.text, "MediaBox");
      if (!box.empty()) {
        box = resolve(box, 1);
        double v[4] = {0, 0, 0, 0};
        int k = 0;
        for_items(box, [&](std::string_view x) {
          if (k < 4) v[k++] = num_of(x, 0);
        });
        if (k == 4) {
          y0 = static_cast<float>(std::min(v[1], v[3]));
          y1 = static_cast<float>(std::max(v[1], v[3]));
        }
      }
      const std::string_view kids = dict_get(o.text, "Kids");
      if (!kids.empty() && it.depth < 64) {
        // Pushed in reverse, so the first kid comes off first.
        uint32_t list[1024];
        int n = 0;
        std::string_view items = resolve(kids, 1);
        for_items(items, [&](std::string_view v) {
          uint32_t r;
          if (n < 1024 && ref_of(v, r)) list[n++] = r;
        });
        for (int k = n - 1; k >= 0 && top < kMaxStack; --k) {
          stack_[top++] = {list[k], res, y0, y1, static_cast<uint8_t>(it.depth + 1)};
        }
        continue;
      }
      pages_[page_count_++] = {it.obj, res, y0, y1};
    }
    return true;
  }

  int page_of(uint32_t obj) const {
    for (uint32_t i = 0; i < page_count_; ++i) {
      if (pages_[i].obj == obj) return static_cast<int>(i);
    }
    return -1;
  }

  // The page a destination (array, name or string) leads to, or -1.
  int destination_page(std::string_view dest, int depth = 0) {
    if (dest.empty() || depth > 4) return -1;
    dest = resolve(dest, 2);
    if (dest.empty()) return -1;
    if (dest[0] == '[') {
      int page = -1;
      bool first = true;
      for_items(dest, [&](std::string_view v) {
        if (!first) return;
        first = false;
        uint32_t r;
        if (ref_of(v, r)) {
          page = page_of(r);
        } else {
          const double n = num_of(v, -1);
          if (n >= 0 && n < page_count_) page = static_cast<int>(n);
        }
      });
      return page;
    }
    if (dest[0] == '<') return destination_page(dict_get(dest, "D"), depth + 1);
    // A named destination.
    Lex l(dest);
    const Token t = l.next();
    if (t.kind != Tok::Name && t.kind != Tok::String && t.kind != Tok::Hex) return -1;
    uint8_t key[128];
    const size_t key_len =
        t.kind == Tok::Name ? std::min(t.text.size(), sizeof(key)) : string_bytes(t, key, sizeof(key));
    if (t.kind == Tok::Name) std::memcpy(key, t.text.data(), key_len);
    const std::string_view name(reinterpret_cast<char*>(key), key_len);
    Obj cat;
    if (!fetch(root_, 3, cat)) return -1;
    if (t.kind == Tok::Name) {
      const std::string_view dests = resolve(dict_get(cat.text, "Dests"), 3);
      const std::string_view d = dict_get(dests, name);
      char copy[512];
      const size_t n = std::min(d.size(), sizeof(copy));
      std::memcpy(copy, d.data(), n);
      return destination_page(std::string_view(copy, n), depth + 1);
    }
    std::string_view names = resolve(dict_get(cat.text, "Names"), 3);
    uint32_t tree;
    if (!ref_of(dict_get(names, "Dests"), tree)) return -1;
    return find_name(tree, name, 0, depth);
  }

  int find_name(uint32_t node, std::string_view key, int level, int depth) {
    if (level > 16) return -1;
    Obj o;
    if (!fetch(node, 3, o)) return -1;
    const std::string_view names = dict_get(o.text, "Names");
    if (!names.empty()) {
      bool is_key = true;
      // The value follows its key.
      bool take = false;
      std::string_view result;
      is_key = true;
      for_items(names, [&](std::string_view v) {
        if (!result.empty()) return;
        if (is_key) {
          Lex l(v);
          const Token t = l.next();
          uint8_t k[128];
          const size_t n = string_bytes(t, k, sizeof(k));
          take = std::string_view(reinterpret_cast<char*>(k), n) == key;
        } else if (take) {
          result = v;
        }
        is_key = !is_key;
      });
      char copy[256];
      const size_t n = std::min(result.size(), sizeof(copy));
      std::memcpy(copy, result.data(), n);
      return destination_page(std::string_view(copy, n), depth + 1);
    }
    uint32_t kids[64];
    int count = 0;
    for_items(dict_get(o.text, "Kids"), [&](std::string_view v) {
      uint32_t r;
      if (count < 64 && ref_of(v, r)) kids[count++] = r;
    });
    for (int k = 0; k < count; ++k) {
      Obj kid;
      if (!fetch(kids[k], 3, kid)) continue;
      // Only the kid whose limits hold the key.
      const std::string_view limits = dict_get(kid.text, "Limits");
      if (!limits.empty()) {
        std::string_view lo, hi;
        int i = 0;
        for_items(limits, [&](std::string_view v) { (i++ == 0 ? lo : hi) = v; });
        uint8_t a[128], b[128];
        Lex la(lo), lb(hi);
        const Token ta = la.next(), tb = lb.next();
        const size_t na = string_bytes(ta, a, sizeof(a)), nb = string_bytes(tb, b, sizeof(b));
        if (key < std::string_view(reinterpret_cast<char*>(a), na) ||
            key > std::string_view(reinterpret_cast<char*>(b), nb)) {
          continue;
        }
      }
      const int p = find_name(kids[k], key, level + 1, depth);
      if (p >= 0) return p;
    }
    return -1;
  }

  void read_outline() {
    outline_count_ = 0;
    Obj cat;
    if (!fetch(root_, 0, cat)) return;
    uint32_t outlines;
    if (!ref_of(dict_get(cat.text, "Outlines"), outlines)) return;
    Obj top;
    if (!fetch(outlines, 0, top)) return;
    uint32_t first;
    if (!ref_of(dict_get(top.text, "First"), first)) return;
    struct Item {
      uint32_t obj;
      uint8_t depth;
    } stack[64];
    int n = 0;
    stack[n++] = {first, 0};
    uint32_t seen = 0;
    while (n > 0 && outline_count_ < kMaxMarks && seen++ < 4096) {
      const Item it = stack[--n];
      Obj o;
      if (!fetch(it.obj, 0, o)) continue;
      // Copied out: the slot is needed to follow the destination.
      char text[2048];
      const size_t len = std::min(o.text.size(), sizeof(text));
      std::memcpy(text, o.text.data(), len);
      const std::string_view item(text, len);
      uint32_t next, child;
      if (ref_of(dict_get(item, "Next"), next) && n < 64) stack[n++] = {next, it.depth};
      if (ref_of(dict_get(item, "First"), child) && n < 64 && it.depth < 6) {
        stack[n++] = {child, static_cast<uint8_t>(it.depth + 1)};
      }
      std::string_view dest = dict_get(item, "Dest");
      if (dest.empty()) {
        const std::string_view action = resolve(dict_get(item, "A"), 1);
        char act[1024];
        const size_t an = std::min(action.size(), sizeof(act));
        std::memcpy(act, action.data(), an);
        const std::string_view a(act, an);
        if (name_of(dict_get(a, "S")) == "GoTo") {
          const std::string_view d = dict_get(a, "D");
          char dbuf[512];
          const size_t dn = std::min(d.size(), sizeof(dbuf));
          std::memcpy(dbuf, d.data(), dn);
          add_outline(item, destination_page(std::string_view(dbuf, dn)), it.depth);
        }
        continue;
      }
      char dbuf[512];
      const size_t dn = std::min(dest.size(), sizeof(dbuf));
      std::memcpy(dbuf, dest.data(), dn);
      add_outline(item, destination_page(std::string_view(dbuf, dn)), it.depth);
    }
    // In page order (the walk above is in outline order already, nearly always).
    std::stable_sort(outline_, outline_ + outline_count_,
                     [](const Outline& a, const Outline& b) { return a.page < b.page; });
  }

  void add_outline(std::string_view item, int page, int depth) {
    if (page < 0 || outline_count_ >= kMaxMarks) return;
    Lex l(dict_get(item, "Title"));
    const Token t = l.next();
    Outline& o = outline_[outline_count_];
    o.page = static_cast<uint32_t>(page);
    o.depth = static_cast<uint8_t>(depth);
    char title[256];
    text_string(t, title, sizeof(title));
    const std::string_view s = trim(title);
    size_t n = std::min(s.size(), sizeof(o.title) - 1);
    while (n > 0 && n < s.size() && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
    std::memcpy(o.title, s.data(), n);
    o.title[n] = '\0';
    if (n > 0) ++outline_count_;
  }

  void read_info(char* title, char* author, char* language) {
    std::memcpy(language, lang_, sizeof(lang_));
    Obj o;
    if (info_ == kNoObject || !fetch(info_, 0, o)) return;
    for (auto [key, out] : {std::pair<const char*, char*>{"Title", title}, {"Author", author}}) {
      Lex l(resolve(dict_get(o.text, key), 1));
      const Token t = l.next();
      if (t.kind != Tok::String && t.kind != Tok::Hex) continue;
      char buf[256];
      text_string(t, buf, sizeof(buf));
      std::string_view s = trim(buf);
      // "Microsoft Word - Book.docx" says nothing the file name does not.
      if (s.starts_with("Microsoft Word - ")) s.remove_prefix(17);
      if (ends_with_lower(s, ".doc") || ends_with_lower(s, ".docx") || ends_with_lower(s, ".pdf") ||
          ends_with_lower(s, ".indd") || s == "untitled" || s == "Untitled") {
        s = {};
      }
      const size_t n = std::min<size_t>(s.size(), 159);
      std::memcpy(out, s.data(), n);
      out[n] = '\0';
    }
  }

  // ---- Fonts ----

  // Font table of a page or form: resource name to font index.
  struct FontName {
    char name[32];
    int16_t font;
  };
  static constexpr int kMaxFontNames = 64;

  uint32_t* alloc(uint32_t words) {
    if (pool_used_ + words > kPoolWords) {
      pool_full_ = true;
      return nullptr;
    }
    uint32_t* p = pool_ + pool_used_;
    pool_used_ += words;
    return p;
  }

  int font_index(uint32_t obj) {
    for (int i = 0; i < font_count_; ++i) {
      if (fonts_[i].obj == obj) return i;
    }
    if (font_count_ >= kMaxFonts) return -1;
    Font& f = fonts_[font_count_];
    f = Font{};
    f.obj = obj;
    if (!load_font(obj, f)) {
      f.map = 0;
      f.ranges = 0;
    }
    return font_count_++;
  }

  // Fills a font table from a /Font resource dictionary (text in a slot).
  int read_font_names(std::string_view fonts, FontName* out) {
    int n = 0;
    Lex l(fonts);
    if (l.next().kind != Tok::DictOpen) return 0;
    struct Pending {
      char name[32];
      uint32_t obj;
    } pending[kMaxFontNames];
    for (;;) {
      const Token k = l.next();
      if (k.kind == Tok::DictClose || k.kind == Tok::End || n >= kMaxFontNames) break;
      if (k.kind != Tok::Name) continue;
      const std::string_view v = l.value();
      uint32_t r;
      if (!ref_of(v, r) || k.text.size() >= sizeof(pending[0].name)) continue;
      std::memcpy(pending[n].name, k.text.data(), k.text.size());
      pending[n].name[k.text.size()] = '\0';
      pending[n].obj = r;
      ++n;
    }
    // Loaded only now: loading reads other objects into the slots.
    for (int attempt = 0; attempt < 2; ++attempt) {
      pool_full_ = false;
      for (int i = 0; i < n; ++i) {
        std::memcpy(out[i].name, pending[i].name, sizeof(out[i].name));
        out[i].font = static_cast<int16_t>(font_index(pending[i].obj));
      }
      if (!pool_full_ && font_count_ < kMaxFonts) break;
      // Out of room: start the cache again with this page's fonts.
      font_count_ = 0;
      pool_used_ = 16;
    }
    return n;
  }

  // Standard widths for the base fonts when a file leaves them out.
  static const uint16_t* standard_widths(std::string_view base, bool& mono) {
    mono = base.find("Courier") != std::string_view::npos;
    const bool bold = base.find("Bold") != std::string_view::npos;
    if (base.find("Times") != std::string_view::npos || base.find("Serif") != std::string_view::npos) {
      return bold ? kTimesBold : kTimes;
    }
    return bold ? kHelveticaBold : kHelvetica;
  }

  bool load_font(uint32_t obj, Font& f) {
    Obj o;
    if (!fetch(obj, 2, o)) return false;
    char dict[4096];
    const size_t dn = std::min(o.text.size(), sizeof(dict));
    std::memcpy(dict, o.text.data(), dn);
    const std::string_view d(dict, dn);
    const std::string_view subtype = name_of(dict_get(d, "Subtype"));
    std::string_view base = name_of(dict_get(d, "BaseFont"));
    char base_name[64];
    const size_t bn = std::min(base.size(), sizeof(base_name));
    std::memcpy(base_name, base.data(), bn);
    base = std::string_view(base_name, bn);
    auto style_of = [](std::string_view b) {
      uint8_t s = 0;
      if (b.find("Bold") != std::string_view::npos || b.find("Black") != std::string_view::npos ||
          b.find("Heavy") != std::string_view::npos || b.find("Semibold") != std::string_view::npos ||
          b.find("SemiBold") != std::string_view::npos || b.find(",Bold") != std::string_view::npos) {
        s |= 1;
      }
      if (b.find("Italic") != std::string_view::npos || b.find("Oblique") != std::string_view::npos) {
        s |= 2;
      }
      return s;
    };
    f.style = style_of(base);
    uint32_t to_unicode = kNoObject;
    uint32_t r;
    if (ref_of(dict_get(d, "ToUnicode"), r)) to_unicode = r;

    if (subtype == "Type0") {
      f.two_byte = true;
      // Widths from the descendant CID font.
      std::string_view desc = resolve(dict_get(d, "DescendantFonts"), 2);
      uint32_t cid = kNoObject;
      for_items(desc, [&](std::string_view v) {
        uint32_t x;
        if (cid == kNoObject && ref_of(v, x)) cid = x;
      });
      Obj c;
      if (cid != kNoObject && fetch(cid, 2, c)) {
        f.default_width = static_cast<uint16_t>(std::clamp(num_of(dict_get(c.text, "DW"), 1000), 0.0, 4000.0));
        char wbuf[kSlotBytes / 2];
        const std::string_view wv = resolve(dict_get(c.text, "W"), 3);
        const size_t wn = std::min(wv.size(), sizeof(wbuf));
        std::memcpy(wbuf, wv.data(), wn);
        read_cid_widths(std::string_view(wbuf, wn), f);
      }
      if (to_unicode != kNoObject) read_to_unicode(to_unicode, f);
      return true;
    }

    // A simple font: 256 codes.
    uint32_t* map_at = alloc(256);
    uint32_t* widths_at = alloc(128);  // 256 16-bit widths
    if (map_at == nullptr || widths_at == nullptr) return false;
    f.map = static_cast<uint32_t>(map_at - pool_);
    f.widths = static_cast<uint32_t>(widths_at - pool_);
    uint32_t* map = pool_ + f.map;
    auto* widths = reinterpret_cast<uint16_t*>(pool_ + f.widths);
    std::memset(widths, 0, 512);
    // The base encoding.
    std::string_view enc = dict_get(d, "Encoding");
    char enc_buf[8192];
    size_t en = 0;
    if (!enc.empty()) {
      const std::string_view e = resolve(enc, 3);
      en = std::min(e.size(), sizeof(enc_buf));
      std::memcpy(enc_buf, e.data(), en);
    }
    const std::string_view e(enc_buf, en);
    std::string_view base_enc = name_of(e);
    if (base_enc.empty() && !e.empty() && e[0] == '<') base_enc = name_of(dict_get(e, "BaseEncoding"));
    const bool standard = base_enc == "StandardEncoding" || (base_enc.empty() && subtype == "Type1");
    const bool builtin = e.empty() && to_unicode == kNoObject && subtype != "TrueType";
    for (uint32_t c = 0; c < 256; ++c) {
      map[c] = c < 0x20 ? 0 : charset::code_point(charset::Encoding::Windows1252, static_cast<uint8_t>(c));
    }
    if (standard) {
      map[0x27] = 0x2019;
      map[0x60] = 0x2018;
    }
    if (builtin) {
      // TeX's fonts: ligatures and quotes in the low codes.
      map[0x0B] = 0xFB00;
      map[0x0C] = 0xFB01;
      map[0x0D] = 0xFB02;
      map[0x0E] = 0xFB03;
      map[0x0F] = 0xFB04;
      map[0x7B] = subtype == "Type1" && base.find("CMR") != std::string_view::npos ? 0x2013 : map[0x7B];
      map[0x7C] = subtype == "Type1" && base.find("CMR") != std::string_view::npos ? 0x2014 : map[0x7C];
      map[0x5C] = base.find("CMR") != std::string_view::npos ? 0x201C : map[0x5C];
      map[0x22] = base.find("CMR") != std::string_view::npos ? 0x201D : map[0x22];
    }
    // /Differences: a code, then glyph names for it and the codes after.
    if (!e.empty() && e[0] == '<') {
      uint32_t code = 0;
      for_items(dict_get(e, "Differences"), [&](std::string_view v) {
        Lex l(v);
        const Token t = l.next();
        if (t.kind == Tok::Number) {
          code = static_cast<uint32_t>(t.num);
        } else if (t.kind == Tok::Name && code < 256) {
          map[code++] = glyph_code_point(t.text);
        }
      });
    }
    // Widths, in thousandths of the size (Type3: in glyph space).
    const uint32_t first = static_cast<uint32_t>(num_of(dict_get(d, "FirstChar"), 0));
    double scale = 1;
    if (subtype == "Type3") {
      for_items(dict_get(d, "FontMatrix"), [&](std::string_view v) {
        if (scale == 1) scale = num_of(v, 0.001) * 1000;
      });
    }
    uint32_t code = first;
    const std::string_view wv = resolve(dict_get(d, "Widths"), 3);
    for_items(wv, [&](std::string_view v) {
      if (code < 256) widths[code] = static_cast<uint16_t>(std::clamp(num_of(v, 0) * scale, 0.0, 4000.0));
      ++code;
      f.has_widths = true;
    });
    if (!f.has_widths) {
      bool mono = false;
      const uint16_t* std_w = standard_widths(base, mono);
      for (uint32_t c = 0; c < 256; ++c) widths[c] = mono ? 600 : c >= 32 && c < 127 ? std_w[c - 32] : 500;
      f.has_widths = true;
    }
    if (to_unicode != kNoObject) read_to_unicode(to_unicode, f);
    return true;
  }

  // /W: "c [w1 w2 ...]" or "c1 c2 w".
  void read_cid_widths(std::string_view w, Font& f) {
    Lex l(w);
    if (l.next().kind != Tok::ArrayOpen) return;
    const uint32_t start = pool_used_;
    uint32_t count = 0;
    auto add = [&](uint32_t lo, uint32_t hi, double width) {
      uint32_t* p = alloc(2);
      if (p == nullptr) return;
      p[0] = std::min<uint32_t>(lo, 0xFFFF) << 16 | std::min<uint32_t>(hi, 0xFFFF);
      p[1] = static_cast<uint32_t>(std::clamp(width, 0.0, 4000.0));
      ++count;
    };
    for (;;) {
      const Token a = l.next();
      if (a.kind != Tok::Number) break;
      l.ws();
      const size_t p = l.pos();
      if (p < w.size() && w[p] == '[') {
        l.next();
        uint32_t c = static_cast<uint32_t>(a.num);
        for (;;) {
          const Token t = l.next();
          if (t.kind != Tok::Number) break;
          add(c, c, t.num);
          ++c;
        }
      } else {
        const Token b = l.next(), c = l.next();
        if (b.kind != Tok::Number || c.kind != Tok::Number) break;
        add(static_cast<uint32_t>(a.num), static_cast<uint32_t>(b.num), c.num);
      }
    }
    f.wranges = start;
    f.wrange_count = count;
    sort_pairs(pool_ + start, count);
  }

  static void sort_pairs(uint32_t* p, uint32_t count) {
    struct Pair {
      uint32_t key, value;
    };
    auto* pairs = reinterpret_cast<Pair*>(p);
    std::stable_sort(pairs, pairs + count, [](const Pair& a, const Pair& b) { return (a.key >> 16) < (b.key >> 16); });
  }

  // Code points from a UTF-16BE hex string: one, or a sequence kept in the pool.
  uint32_t unicode_of(const Token& t) {
    uint8_t raw[32];
    const size_t n = string_bytes(t, raw, sizeof(raw));
    uint32_t cps[8];
    int count = 0;
    for (size_t i = 0; i + 1 < n && count < 8; i += 2) {
      uint32_t cp = static_cast<uint32_t>(raw[i] << 8 | raw[i + 1]);
      if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < n) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(raw[i + 2] << 8 | raw[i + 3]) - 0xDC00);
        i += 2;
      }
      cps[count++] = cp;
    }
    if (n == 1) cps[count++] = raw[0];
    if (count == 0) return 0;
    if (count == 1) return cps[0];
    uint32_t* p = alloc(static_cast<uint32_t>(count) + 1);
    if (p == nullptr) return cps[0];
    p[0] = static_cast<uint32_t>(count);
    for (int k = 0; k < count; ++k) p[k + 1] = cps[k];
    return kSequence | static_cast<uint32_t>(p - pool_);
  }

  void read_to_unicode(uint32_t obj, Font& f) {
    Obj o;
    if (!fetch(obj, 3, o) || !o.stream) return;
    // Read into the object stream buffer, which is then no longer cached.
    objstm_num_ = kNoObject;
    const int32_t n = stream_data(o, objstm_, kObjStmBytes);
    if (n <= 0) return;
    const std::string_view cmap(reinterpret_cast<char*>(objstm_), static_cast<size_t>(n));
    Lex l(cmap);
    uint32_t* map = f.two_byte ? nullptr : pool_ + f.map;
    const uint32_t start = pool_used_;
    uint32_t count = 0;
    auto code_of = [](const Token& t, int& bytes) {
      uint8_t raw[4];
      const size_t k = string_bytes(t, raw, sizeof(raw));
      bytes = static_cast<int>(k);
      uint32_t v = 0;
      for (size_t i = 0; i < k; ++i) v = v << 8 | raw[i];
      return v;
    };
    // Ranges are written as (lo << 16 | hi, code point), single codes as lo == hi.
    auto add = [&](uint32_t lo, uint32_t hi, uint32_t cp) {
      if (map != nullptr) {
        for (uint32_t c = lo; c <= hi && c < 256; ++c) {
          map[c] = (cp & kSequence) ? cp : cp + (c - lo);
          if (cp & kSequence) break;
        }
        return;
      }
      uint32_t* p = alloc(2);
      if (p == nullptr) return;
      p[0] = std::min<uint32_t>(lo, 0xFFFF) << 16 | std::min<uint32_t>(hi, 0xFFFF);
      p[1] = cp;
      ++count;
    };
    // Sequences added while ranges are being written would break the run
    // of pairs: those are collected first and written after.
    for (;;) {
      const Token t = l.next();
      if (t.kind == Tok::End) break;
      if (t.kind != Tok::Word) continue;
      if (t.text == "begincodespacerange") {
        for (;;) {
          const Token lo = l.next();
          if (lo.kind != Tok::Hex) break;
          l.next();
        }
      } else if (t.text == "beginbfchar") {
        for (;;) {
          const Token src = l.next();
          if (src.kind != Tok::Hex && src.kind != Tok::String) break;
          const Token dst = l.next();
          int bytes;
          const uint32_t code = code_of(src, bytes);
          uint32_t cp = 0;
          if (dst.kind == Tok::Hex || dst.kind == Tok::String) cp = single_or_sequence(dst, map != nullptr);
          if (dst.kind == Tok::Name) cp = glyph_code_point(dst.text);
          if (cp != 0) add(code, code, cp);
        }
      } else if (t.text == "beginbfrange") {
        for (;;) {
          const Token lo = l.next();
          if (lo.kind != Tok::Hex) break;
          const Token hi = l.next();
          int b1, b2;
          const uint32_t a = code_of(lo, b1), z = code_of(hi, b2);
          l.ws();
          const size_t p = l.pos();
          if (p < cmap.size() && cmap[p] == '[') {
            l.next();
            for (uint32_t c = a;; ++c) {
              const Token d = l.next();
              if (d.kind != Tok::Hex) break;
              const uint32_t cp = single_or_sequence(d, map != nullptr);
              if (c <= z && cp != 0) add(c, c, cp);
            }
          } else {
            const Token d = l.next();
            if (d.kind != Tok::Hex || z < a || z - a > 0xFFFF) continue;
            const uint32_t cp = single_or_sequence(d, map != nullptr);
            if (cp & kSequence) {
              add(a, a, cp);  // a ligature: its first code only
            } else if (cp != 0) {
              add(a, z, cp);
            }
          }
        }
      }
    }
    if (f.two_byte) {
      f.ranges = start;
      f.range_count = count;
      sort_pairs(pool_ + start, count);
    }
  }

  // For two-byte fonts, sequences go in the pool after the pairs being
  // written would break them: a ligature in a two-byte font keeps its first
  // character instead.
  uint32_t single_or_sequence(const Token& t, bool may_store) {
    if (may_store) return unicode_of(t);
    uint8_t raw[8];
    const size_t n = string_bytes(t, raw, sizeof(raw));
    if (n < 2) return n == 1 ? raw[0] : 0;
    uint32_t cp = static_cast<uint32_t>(raw[0] << 8 | raw[1]);
    if (cp >= 0xD800 && cp < 0xDC00 && n >= 4) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(raw[2] << 8 | raw[3]) - 0xDC00);
    } else if (n == 4 && cp < 0x80 && raw[2] == 0 && raw[3] < 0x80) {
      // "fi" written as two characters: the ligature itself.
      const char a = static_cast<char>(cp), b = static_cast<char>(raw[3]);
      if (a == 'f' && b == 'i') return 0xFB01;
      if (a == 'f' && b == 'l') return 0xFB02;
      if (a == 'f' && b == 'f') return 0xFB00;
    }
    return cp;
  }

  // The code point (or sequence) of a code, and its width in thousandths.
  uint32_t char_of(const Font& f, uint32_t code, uint32_t& width) const {
    if (!f.two_byte) {
      width = f.widths != 0 ? reinterpret_cast<const uint16_t*>(pool_ + f.widths)[code & 0xFF] : 500;
      if (width == 0) width = 500;
      return f.map != 0 ? pool_[f.map + (code & 0xFF)] : 0;
    }
    width = f.default_width;
    uint32_t lo = 0, hi = f.wrange_count;
    const uint32_t* w = pool_ + f.wranges;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) / 2;
      if ((w[2 * mid] >> 16) <= code) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo > 0 && code <= (w[2 * (lo - 1)] & 0xFFFF)) width = w[2 * (lo - 1) + 1];
    const uint32_t* r = pool_ + f.ranges;
    lo = 0;
    hi = f.range_count;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) / 2;
      if ((r[2 * mid] >> 16) <= code) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo == 0) return 0;
    const uint32_t key = r[2 * (lo - 1)], cp = r[2 * (lo - 1) + 1];
    if (code > (key & 0xFFFF)) return 0;
    return (cp & kSequence) ? cp : cp + (code - (key >> 16));
  }

  // ---- Reading a page ----

  struct TextState {
    Matrix tm, tlm, ctm;
    double size = 0;
    double char_space = 0, word_space = 0, scale = 1, leading = 0;
    int font = -1;
  };

  void read_page(uint32_t index) {
    const PageRef& pr = pages_[index];
    page_y0_ = pr.y0;
    page_y1_ = pr.y1;
    run_count_ = 0;
    run_used_ = 0;
    // Resources: fonts and forms.
    FontName* names = font_names_[0];
    int name_count = 0;
    char xobjects[4096];
    size_t xn = 0;
    if (pr.res != kNoObject) {
      Obj holder;
      if (fetch(pr.res, 0, holder)) {
        const std::string_view res = resolve(dict_get(holder.text, "Resources"), 1);
        char rbuf[8192];
        const size_t rn = std::min(res.size(), sizeof(rbuf));
        if (rn > 0) std::memcpy(rbuf, res.data(), rn);
        const std::string_view r(rbuf, rn);
        const std::string_view xo = resolve(dict_get(r, "XObject"), 2);
        xn = std::min(xo.size(), sizeof(xobjects));
        if (xn > 0) std::memcpy(xobjects, xo.data(), xn);
        const std::string_view fonts = resolve(dict_get(r, "Font"), 2);
        char fbuf[4096];
        const size_t fn = std::min(fonts.size(), sizeof(fbuf));
        if (fn > 0) std::memcpy(fbuf, fonts.data(), fn);
        name_count = read_font_names(std::string_view(fbuf, fn), names);
      }
    }
    // The content: one stream or several, one after another.
    Obj page;
    if (!fetch(pr.obj, 0, page)) return;
    const std::string_view contents = dict_get(page.text, "Contents");
    uint32_t streams[256];
    int stream_count = 0;
    uint32_t one;
    if (ref_of(contents, one)) {
      Obj c;
      if (fetch(one, 1, c) && !c.stream) {
        // A reference to an array of streams.
        for_items(c.text, [&](std::string_view v) {
          uint32_t r;
          if (stream_count < 256 && ref_of(v, r)) streams[stream_count++] = r;
        });
      } else {
        streams[stream_count++] = one;
      }
    } else {
      for_items(contents, [&](std::string_view v) {
        uint32_t r;
        if (stream_count < 256 && ref_of(v, r)) streams[stream_count++] = r;
      });
    }
    uint32_t used = 0;
    for (int k = 0; k < stream_count; ++k) {
      Obj c;
      if (!fetch(streams[k], 1, c)) continue;
      const int32_t n = stream_data(c, content_ + used, kContentBytes - used - 1);
      if (n <= 0) continue;
      used += static_cast<uint32_t>(n);
      if (used < kContentBytes) content_[used++] = '\n';
    }
    content_used_ = used;
    TextState ts;
    interpret(std::string_view(reinterpret_cast<char*>(content_), used), ts, names, name_count,
              std::string_view(xobjects, xn), 0);
    layout();
  }

  int find_font(const FontName* names, int count, std::string_view name) const {
    for (int i = 0; i < count; ++i) {
      if (name == names[i].name) return names[i].font;
    }
    return -1;
  }

  void interpret(std::string_view s, TextState& ts, const FontName* names, int name_count, std::string_view xobjects,
                 int depth) {
    Lex l(s);
    Token ops[8];
    int nops = 0;
    Matrix saved[32];
    int saved_count = 0;
    for (;;) {
      l.ws();
      const size_t before = l.pos();
      Token t = l.next();
      if (t.kind == Tok::End) break;
      if (t.kind == Tok::ArrayOpen || t.kind == Tok::DictOpen) {
        l.set_pos(before);
        const std::string_view v = l.value();
        t.text = v;
        if (nops < 8) ops[nops++] = t;
        continue;
      }
      if (t.kind != Tok::Word) {
        if (nops < 8) {
          ops[nops++] = t;
        } else {
          std::memmove(ops, ops + 1, sizeof(Token) * 7);
          ops[7] = t;
        }
        continue;
      }
      const std::string_view op = t.text;
      auto num = [&](int i) { return i < nops && ops[i].kind == Tok::Number ? ops[i].num : 0.0; };
      auto next_line = [&](double tx, double ty) {
        ts.tlm = Matrix{1, 0, 0, 1, tx, ty}.times(ts.tlm);
        ts.tm = ts.tlm;
        line_break_ = true;
      };
      if (op == "BT") {
        ts.tm = ts.tlm = Matrix{};
      } else if (op == "Tf" && nops >= 2) {
        ts.font = ops[0].kind == Tok::Name ? find_font(names, name_count, ops[0].text) : -1;
        ts.size = num(1);
      } else if (op == "Tc" && nops >= 1) {
        ts.char_space = num(0);
      } else if (op == "Tw" && nops >= 1) {
        ts.word_space = num(0);
      } else if (op == "Tz" && nops >= 1) {
        ts.scale = num(0) / 100;
      } else if (op == "TL" && nops >= 1) {
        ts.leading = num(0);
      } else if (op == "Td" && nops >= 2) {
        next_line(num(0), num(1));
      } else if (op == "TD" && nops >= 2) {
        ts.leading = -num(1);
        next_line(num(0), num(1));
      } else if (op == "Tm" && nops >= 6) {
        ts.tm = ts.tlm = Matrix{num(0), num(1), num(2), num(3), num(4), num(5)};
      } else if (op == "T*") {
        next_line(0, -ts.leading);
      } else if (op == "Tj" && nops >= 1) {
        show(ts, ops[nops - 1], false);
      } else if (op == "'" && nops >= 1) {
        next_line(0, -ts.leading);
        show(ts, ops[nops - 1], false);
      } else if (op == "\"" && nops >= 3) {
        ts.word_space = num(0);
        ts.char_space = num(1);
        next_line(0, -ts.leading);
        show(ts, ops[2], false);
      } else if (op == "TJ" && nops >= 1 && ops[nops - 1].kind == Tok::ArrayOpen) {
        Lex a(ops[nops - 1].text);
        a.next();
        bool first = true;
        for (;;) {
          const Token item = a.next();
          if (item.kind == Tok::ArrayClose || item.kind == Tok::End) break;
          if (item.kind == Tok::Number) {
            const double tx = -item.num / 1000 * ts.size * ts.scale;
            ts.tm.e += tx * ts.tm.a;
            ts.tm.f += tx * ts.tm.b;
            if (-item.num > 180) space_pending_ = true;
          } else if (item.kind == Tok::String || item.kind == Tok::Hex) {
            show(ts, item, !first);
            first = false;
          }
        }
      } else if (op == "cm" && nops >= 6) {
        ts.ctm = Matrix{num(0), num(1), num(2), num(3), num(4), num(5)}.times(ts.ctm);
      } else if (op == "q") {
        if (saved_count < 32) saved[saved_count++] = ts.ctm;
      } else if (op == "Q") {
        if (saved_count > 0) ts.ctm = saved[--saved_count];
      } else if (op == "BI") {
        // An inline picture: its data runs from "ID" to "EI".
        const size_t id = s.find("ID", l.pos());
        if (id == std::string_view::npos) break;
        size_t k = id + 3;
        for (;;) {
          k = s.find("EI", k);
          if (k == std::string_view::npos) break;
          const bool before_ok = k > 0 && is_ws(static_cast<uint8_t>(s[k - 1]));
          const bool after_ok = k + 2 >= s.size() || is_ws(static_cast<uint8_t>(s[k + 2]));
          if (before_ok && after_ok) break;
          k += 2;
        }
        if (k == std::string_view::npos) break;
        l.set_pos(k + 2);
      } else if (op == "Do" && nops >= 1 && ops[0].kind == Tok::Name && depth < 2 && !xobjects.empty()) {
        form(ts, ops[0].text, names, name_count, xobjects, depth);
      }
      nops = 0;
    }
  }

  // A form: its content drawn as if it were part of the page.
  void form(TextState& ts, std::string_view name, const FontName* names, int name_count, std::string_view xobjects,
            int depth) {
    uint32_t r;
    if (!ref_of(dict_get(xobjects, name), r)) return;
    Obj o;
    if (!fetch(r, 1, o) || !o.stream || name_of(dict_get(o.text, "Subtype")) != "Form") return;
    Matrix m;
    int k = 0;
    double v[6] = {1, 0, 0, 1, 0, 0};
    for_items(dict_get(o.text, "Matrix"), [&](std::string_view x) {
      if (k < 6) v[k++] = num_of(x, 0);
    });
    if (k == 6) m = Matrix{v[0], v[1], v[2], v[3], v[4], v[5]};
    // Its own fonts and forms, if it names them.
    FontName* own = font_names_[depth + 1];
    int own_count = 0;
    char xbuf[2048];
    size_t xn = 0;
    const std::string_view res_v = dict_get(o.text, "Resources");
    char obuf[2048];
    const size_t on = std::min(o.text.size(), sizeof(obuf));
    std::memcpy(obuf, o.text.data(), on);
    Obj copy = o;
    copy.text = std::string_view(obuf, on);
    if (!res_v.empty()) {
      const std::string_view res = resolve(dict_get(copy.text, "Resources"), 2);
      char rbuf[4096];
      const size_t rn = std::min(res.size(), sizeof(rbuf));
      std::memcpy(rbuf, res.data(), rn);
      const std::string_view rr(rbuf, rn);
      const std::string_view xo = resolve(dict_get(rr, "XObject"), 3);
      xn = std::min(xo.size(), sizeof(xbuf));
      std::memcpy(xbuf, xo.data(), xn);
      const std::string_view fonts = resolve(dict_get(rr, "Font"), 3);
      char fbuf[2048];
      const size_t fn = std::min(fonts.size(), sizeof(fbuf));
      std::memcpy(fbuf, fonts.data(), fn);
      own_count = read_font_names(std::string_view(fbuf, fn), own);
    }
    if (content_used_ + 1024 >= kContentBytes) return;
    const int32_t n = stream_data(copy, content_ + content_used_, kContentBytes - content_used_);
    if (n <= 0) return;
    const std::string_view body(reinterpret_cast<char*>(content_ + content_used_), static_cast<size_t>(n));
    content_used_ += static_cast<uint32_t>(n);
    TextState inner = ts;
    inner.ctm = m.times(ts.ctm);
    if (own_count > 0) {
      interpret(body, inner, own, own_count, std::string_view(xbuf, xn), depth + 1);
    } else {
      interpret(body, inner, names, name_count, xn > 0 ? std::string_view(xbuf, xn) : xobjects, depth + 1);
    }
    content_used_ -= static_cast<uint32_t>(n);
  }

  // Shows a string: its characters go into a run, the pen moves on.
  void show(TextState& ts, const Token& str, bool same_run) {
    if (ts.font < 0 || ts.font >= font_count_) return;
    const Font& f = fonts_[ts.font];
    uint8_t bytes[2048];
    const size_t n = string_bytes(str, bytes, sizeof(bytes));
    const Matrix start = ts.tm.times(ts.ctm);
    const double size = std::fabs(ts.size) * std::sqrt(start.c * start.c + start.d * start.d);
    // Too small to be text a reader reads (or nothing at all): only the pen moves.
    const bool visible = size >= 0.5 && size <= 500;
    Run* run = nullptr;
    if (!visible) {
      run = nullptr;
    } else if (same_run && run_count_ > 0 && !line_break_) {
      run = &runs_[run_count_ - 1];
    } else if (run_count_ < kMaxRuns) {
      run = &runs_[run_count_++];
      run->x0 = static_cast<float>(start.e);
      run->y = static_cast<float>(start.f);
      run->size = static_cast<float>(size);
      run->text = run_used_;
      run->len = 0;
      run->style = f.style;
      line_break_ = false;
    }
    auto put = [&](uint32_t cp) {
      if (run == nullptr || run_used_ + 4 >= kRunText || run->len > 60000) return;
      if (cp == 0xA0 || cp == '\t') cp = ' ';
      if (cp < 0x20 || (cp >= 0xFFF0 && cp <= 0xFFFF) || (cp >= 0xE000 && cp < 0xF900))
        return;  // controls, private use
      const size_t k = xml::encode_utf8(cp, run_text_ + run_used_);
      run_used_ += static_cast<uint32_t>(k);
      run->len = static_cast<uint16_t>(run->len + k);
    };
    if (space_pending_ && run != nullptr && run->len > 0 && run_text_[run_used_ - 1] != ' ') put(' ');
    space_pending_ = false;
    const size_t step = f.two_byte ? 2 : 1;
    for (size_t i = 0; i + step <= n; i += step) {
      const uint32_t code = f.two_byte ? static_cast<uint32_t>(bytes[i] << 8 | bytes[i + 1]) : bytes[i];
      uint32_t width;
      const uint32_t cp = char_of(f, code, width);
      if (cp & kSequence) {
        const uint32_t at = cp & ~kSequence;
        const uint32_t count = at < kPoolWords ? std::min<uint32_t>(pool_[at], 8) : 0;
        for (uint32_t k = 0; k < count; ++k) put(pool_[at + 1 + k]);
      } else if (cp != 0) {
        put(cp);
        if (!probe_) ++chars_;
      }
      double tx = width / 1000.0 * ts.size + ts.char_space;
      if (step == 1 && code == 32) tx += ts.word_space;
      tx *= ts.scale;
      ts.tm.e += tx * ts.tm.a;
      ts.tm.f += tx * ts.tm.b;
    }
    if (run != nullptr) run->x1 = static_cast<float>(ts.tm.times(ts.ctm).e);
  }

  // ---- From lines to paragraphs ----

  static uint32_t first_code_point(std::string_view s) {
    if (s.empty()) return 0;
    const auto c = static_cast<uint8_t>(s[0]);
    if (c < 0x80) return c;
    if (c >= 0xC0 && c < 0xE0 && s.size() >= 2) return static_cast<uint32_t>((c & 0x1F) << 6 | (s[1] & 0x3F));
    if (c >= 0xE0 && c < 0xF0 && s.size() >= 3) {
      return static_cast<uint32_t>((c & 0x0F) << 12 | (s[1] & 0x3F) << 6 | (s[2] & 0x3F));
    }
    return 0xFFFF;
  }

  static uint32_t last_code_point(std::string_view s) {
    size_t i = s.size();
    while (i > 0 && (static_cast<uint8_t>(s[i - 1]) & 0xC0) == 0x80) --i;
    return i > 0 ? first_code_point(s.substr(i - 1)) : 0;
  }

  static bool is_lower(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 0xDF && cp <= 0xFF) return cp != 0xF7;
    if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) return (cp & 1) == 0;
    if (cp >= 0x100 && cp <= 0x17F) return (cp & 1) == 1;
    return cp >= 0x430 && cp <= 0x45F;
  }

  static bool is_letter(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) || (cp >= 0x370 && cp <= 0x4FF);
  }

  // A line's words without styles, for comparing and classifying.
  size_t plain_text(const Line& line, char* out, size_t cap) const {
    size_t n = 0;
    for (uint32_t r = line.first; r < static_cast<uint32_t>(line.first) + line.count && n + 2 < cap; ++r) {
      const Run& run = runs_[r];
      if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
      const size_t k = std::min<size_t>(run.len, cap - 2 - n);
      std::memcpy(out + n, run_text_ + run.text, k);
      n += k;
    }
    out[n] = '\0';
    return n;
  }

  // Page numbers: a number or Roman numeral alone ("12", "- 12 -", "xiv").
  static bool is_page_number(std::string_view s) {
    int digits = 0, roman = 0, other = 0;
    for (char c : s) {
      if (c >= '0' && c <= '9') {
        ++digits;
      } else if (std::strchr("ivxlcdmIVXLCDM", c) != nullptr && c != '\0') {
        ++roman;
      } else if (c != ' ' && c != '-' && c != '.' && c != '|' && c != '/') {
        ++other;
      }
    }
    return other == 0 && ((digits > 0 && digits <= 5 && roman == 0) || (digits == 0 && roman > 0 && roman <= 7));
  }

  // Letters only, in lower case: running heads compare by this.
  static uint32_t signature(std::string_view s) {
    uint32_t h = 2166136261u;
    int letters = 0;
    for (char c : s) {
      const char l = ascii_lower(c);
      if ((l >= 'a' && l <= 'z') || static_cast<uint8_t>(c) >= 0x80) {
        h = (h ^ static_cast<uint8_t>(l)) * 16777619u;
        ++letters;
      }
    }
    return letters >= 3 ? h : 0;
  }

  void layout() {
    // Strings into lines: one goes on a line when it sits on the same
    // baseline, to the right of what is there.
    int lc = 0;
    for (int i = 0; i < run_count_; ++i) {
      const Run& r = runs_[i];
      if (lc > 0) {
        Line& l = lines_[lc - 1];
        const float tol = 0.45f * std::max(r.size, l.size);
        if (std::fabs(r.y - l.y) <= tol && r.x0 >= l.x1 - 0.7f * l.size) {
          l.x1 = std::max(l.x1, r.x1);
          l.count = static_cast<uint16_t>(i - l.first + 1);
          if (r.len > 0 && r.len * r.size > l.chars * l.size * 0.5f && r.size > l.size) l.size = r.size;
          l.chars += r.len;
          continue;
        }
      }
      if (r.len == 0) continue;
      Line& l = lines_[lc++];
      l = {r.x0, r.x1, r.y, r.size, static_cast<uint16_t>(i), 1, r.len, false, false};
    }
    if (lc == 0) return;

    // The body text: its size, margins and line spacing.
    Count sizes[32];
    int size_count = 0;
    uint32_t page_chars = 0;
    auto tally = [](Count* c, int& count, int max, float key, uint32_t n) {
      for (int k = 0; k < count; ++k) {
        if (std::fabs(c[k].key - key) < 0.3f) {
          c[k].n += n;
          return;
        }
      }
      if (count < max) c[count++] = {key, n};
    };
    auto mode = [](const Count* c, int count, uint32_t* n = nullptr) {
      float best = 0;
      uint32_t most = 0;
      for (int k = 0; k < count; ++k) {
        if (c[k].n > most) {
          most = c[k].n;
          best = c[k].key;
        }
      }
      if (n != nullptr) *n = most;
      return best;
    };
    for (int i = 0; i < lc; ++i) {
      tally(sizes, size_count, 32, std::round(lines_[i].size * 2) / 2, lines_[i].chars);
      tally(doc_sizes_, doc_size_count_, 32, std::round(lines_[i].size * 2) / 2, lines_[i].chars);
      page_chars += lines_[i].chars;
    }
    float body = mode(sizes, size_count);
    if (page_chars < 400 && doc_size_count_ > 0) body = mode(doc_sizes_, doc_size_count_);
    if (body <= 0) body = 10;
    auto is_body = [&](const Line& l) { return std::fabs(l.size - body) < 0.15f * body; };
    Count lefts[64], rights[64];
    int left_count = 0, right_count = 0, body_lines = 0;
    float max_right = 0, min_left = 1e9f;
    for (int i = 0; i < lc; ++i) {
      const Line& l = lines_[i];
      if (!is_body(l)) continue;
      ++body_lines;
      tally(lefts, left_count, 64, std::round(l.x0), l.chars > 20 ? 1 : 0);
      tally(rights, right_count, 64, std::round(l.x1 / 2) * 2, 1);
      max_right = std::max(max_right, l.x1);
      min_left = std::min(min_left, l.x0);
    }
    float left = left_count > 0 ? mode(lefts, left_count) : min_left;
    if (left <= 0 || left > 1e8f) left = min_left < 1e8f ? min_left : 0;
    uint32_t right_n = 0;
    float right = mode(rights, right_count, &right_n);
    if (right_n < 3) right = max_right;
    // Ragged lines (not justified) end short anywhere: only a very short one ends a paragraph.
    const bool ragged = right_n * 2 < static_cast<uint32_t>(body_lines);
    // Line spacing: the most common step between body lines.
    Count steps[32];
    int step_count = 0;
    for (int i = 1; i < lc; ++i) {
      const float dy = lines_[i - 1].y - lines_[i].y;
      if (is_body(lines_[i]) && is_body(lines_[i - 1]) && dy > 0.5f * body && dy < 3 * body) {
        tally(steps, step_count, 32, std::round(dy * 2) / 2, 1);
      }
    }
    float spacing = step_count > 0 ? mode(steps, step_count) : 1.2f * body;
    if (spacing < 0.9f * body || spacing > 2.5f * body) spacing = 1.2f * body;

    // Page numbers and running heads, at the top and bottom.
    const float h = page_y1_ - page_y0_;
    int top = -1, bottom = -1;
    for (int i = 0; i < lc; ++i) {
      if (top < 0 || lines_[i].y > lines_[top].y) top = i;
      if (bottom < 0 || lines_[i].y < lines_[bottom].y) bottom = i;
    }
    char text[512];
    for (int i = 0; i < lc; ++i) {
      Line& l = lines_[i];
      const bool edge = h > 0 && (l.y > page_y1_ - 0.12f * h || l.y < page_y0_ + 0.1f * h);
      if (!edge) continue;
      const size_t n = plain_text(l, text, sizeof(text));
      if (is_page_number(trim({text, n}))) l.drop = true;
    }
    uint32_t sig_top = 0, sig_bottom = 0;
    if (h > 0 && lines_[top].y > page_y1_ - 0.12f * h) {
      sig_top = signature({text, plain_text(lines_[top], text, sizeof(text))});
      // Large type is a chapter's title ("Chapter 2" is "Chapter 1" without its digits), not a running head.
      if (sig_top != 0 && (sig_top == heads_[0] || sig_top == heads_[1]) && lines_[top].size < 1.15f * body) {
        lines_[top].drop = true;
      }
    }
    if (h > 0 && lines_[bottom].y < page_y0_ + 0.1f * h && bottom != top) {
      sig_bottom = signature({text, plain_text(lines_[bottom], text, sizeof(text))});
      if (sig_bottom != 0 && (sig_bottom == feet_[0] || sig_bottom == feet_[1])) lines_[bottom].drop = true;
    }
    heads_[1] = heads_[0];
    heads_[0] = sig_top;
    feet_[1] = feet_[0];
    feet_[0] = sig_bottom;
    if (probe_) return;

    // Where this page's contents entries go: at headings saying their
    // titles, or else where the first paragraph or heading starts.
    // A line in body type saying a title exactly is that title's heading.
    for (int k = mark_next_, from = 0; k < mark_end_; ++k) {
      mark_line_[k] = -1;
      for (int pass = 0; pass < 2 && mark_line_[k] < 0; ++pass) {
        for (int i = from; i < lc; ++i) {
          Line& l = lines_[i];
          if (l.drop || l.chars <= 2 || l.chars > 160 || (pass == 0 && l.size < 1.25f * body)) continue;
          const std::string_view words = trim({text, plain_text(l, text, sizeof(text))});
          if (pass == 0 ? starts_title(outline_[k].title, words) : starts_title(outline_[k].title, words, true)) {
            mark_line_[k] = static_cast<int16_t>(i);
            l.title = true;
            from = i + 1;
            break;
          }
        }
      }
    }

    // Lines into paragraphs and headings.
    bool first_on_page = true;
    for (int i = 0; i < lc; ++i) {
      const Line& l = lines_[i];
      if (l.drop) continue;
      const bool heading = (l.size >= 1.25f * body && l.chars <= 160 && l.chars > 2) || l.title;
      // A drop cap: a large letter or two that starts the paragraph next to it.
      const bool drop_cap = l.size >= 1.25f * body && l.chars <= 2;
      if (heading) {
        const bool same = kind_ == Kind::Heading && std::fabs(heading_size_ - l.size) < 0.1f * l.size &&
                          prev_y_ - l.y < 2.2f * l.size && prev_y_ - l.y > 0;
        if (!same) {
          flush_marks(i);
          end_paragraph();
          kind_ = Kind::Heading;
          heading_level_ = l.size >= 1.7f * body ? 1 : 2;  // titles in body type: 2
          heading_size_ = l.size;
        }
        append_line(l, false, false);
        prev_y_ = l.y;
        prev_x0_ = l.x0;
        prev_short_ = prev_end_ = true;
        prev_stop_ = false;
        first_on_page = false;
        continue;
      }
      bool start = kind_ != Kind::Paragraph;
      // A sentence ended where this line's first word would still have fit: a new paragraph.
      const bool room = prev_stop_ && right - prev_x1_ > first_word_width(l) + 0.5f * body;
      if (!start && first_on_page) {
        // Going on from the page before, unless that line ended a sentence well
        // short of the margin (ragged lines end short anyway) or this one is indented.
        start = prev_end_ || room || l.x0 > left + 0.6f * body;
      } else if (!start) {
        const float gap = prev_y_ - l.y;
        start = gap > 1.45f * spacing || gap < -0.5f * l.size || l.x0 > prev_x0_ + 0.6f * body || prev_short_ || room;
      }
      if (start && !glue_) {
        flush_marks(i);
        end_paragraph();
        kind_ = Kind::Paragraph;
      }
      append_line(l, glue_, !drop_cap);
      glue_ = drop_cap;
      if (drop_cap) kind_ = Kind::Paragraph;
      const size_t n = plain_text(l, text, sizeof(text));
      const std::string_view words = trim({text, n});
      const float width = std::max(right - left, 10 * body);
      prev_end_ = !drop_cap && sentence_end(last_code_point(words)) && l.x1 < right - 0.3f * width;
      prev_short_ = !drop_cap && (l.x1 < right - 1.5f * body &&
                                  (sentence_end(last_code_point(words)) || l.x1 < right - 0.3f * width));
      if (ragged) prev_short_ = prev_end_;
      prev_stop_ = !drop_cap && sentence_end(last_code_point(words));
      prev_x1_ = l.x1;
      prev_y_ = l.y;
      prev_x0_ = drop_cap ? left : l.x0;
      first_on_page = false;
    }
  }

  // About how wide a line's first word is.
  float first_word_width(const Line& l) const {
    for (uint32_t r = l.first; r < static_cast<uint32_t>(l.first) + l.count; ++r) {
      const Run& run = runs_[r];
      if (run.len == 0) continue;
      const char* t = run_text_ + run.text;
      uint32_t k = 0;
      while (k < run.len && t[k] == ' ') ++k;
      while (k < run.len && t[k] != ' ') ++k;
      return (run.x1 - run.x0) * static_cast<float>(k) / static_cast<float>(run.len);
    }
    return 0;
  }

  // Writes the waiting contents entries that belong before line i (-2: all).
  void flush_marks(int i) {
    while (mark_next_ < mark_end_ && (i == -2 || mark_line_[mark_next_] <= i)) {
      end_paragraph();
      if (page_ != nullptr) page_->chapter(outline_[mark_next_].title, outline_[mark_next_].depth);
      ++mark_next_;
    }
  }

  // Whether a heading's line is the title, or its start (`whole`: all of
  // it); without case or spacing.
  static bool starts_title(const char* title, std::string_view line, bool whole = false) {
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    size_t a = 0, matched = 0;
    const char* b = title;
    for (;;) {
      while (a < line.size() && space(line[a])) ++a;
      while (*b != '\0' && space(*b)) ++b;
      if (a == line.size()) return *b == '\0' || (!whole && matched >= 3);
      if (*b == '\0' || ascii_lower(line[a]) != ascii_lower(*b)) return false;
      ++a;
      ++b;
      ++matched;
    }
  }

  void para(std::string_view s) {
    const size_t n = std::min(s.size(), kParaBytes - 16 - para_len_);
    std::memcpy(para_ + para_len_, s.data(), n);
    para_len_ += n;
  }

  void set_style(uint8_t style) {
    if (style == style_) return;
    if (style_ & 2) para("</i>");
    if (style_ & 1) para("</b>");
    if (style & 1) para("<b>");
    if (style & 2) para("<i>");
    style_ = style;
  }

  // Adds a line's words to the paragraph or heading, joining words broken
  // across lines.
  void append_line(const Line& l, bool glue, bool space) {
    if (para_len_ > 0 && !glue) {
      const std::string_view p(para_, para_len_);
      const uint32_t last = last_code_point(p);
      const Run* first_run = nullptr;
      for (uint32_t r = l.first; r < static_cast<uint32_t>(l.first) + l.count && first_run == nullptr; ++r) {
        if (runs_[r].len > 0) first_run = &runs_[r];
      }
      const uint32_t next = first_run != nullptr ? first_code_point({run_text_ + first_run->text, first_run->len}) : 0;
      const uint32_t before = p.size() >= 2 ? last_code_point(p.substr(0, p.size() - (last == 0xAD ? 2 : 1))) : 0;
      if ((last == '-' || last == 0xAD) && is_letter(before) && is_lower(next)) {
        para_len_ -= last == 0xAD ? 2 : 1;  // the hyphen goes
      } else if (last != ' ' && space) {
        para(" ");
      }
    }
    const bool heading = kind_ == Kind::Heading;
    float x1 = -1e9f;
    for (uint32_t r = l.first; r < static_cast<uint32_t>(l.first) + l.count; ++r) {
      const Run& run = runs_[r];
      if (run.len == 0) continue;
      const std::string_view words(run_text_ + run.text, run.len);
      if (x1 > -1e8f && run.x0 - x1 > 0.15f * run.size && para_len_ > 0 && para_[para_len_ - 1] != ' ' &&
          words[0] != ' ') {
        para(" ");
      }
      if (!heading) set_style(run.style);
      // Escaped for the page.
      size_t from = 0;
      for (size_t k = 0; k < words.size(); ++k) {
        const char c = words[k];
        if (c != '&' && c != '<' && c != '>') continue;
        para(words.substr(from, k - from));
        para(c == '&' ? "&amp;" : c == '<' ? "&lt;" : "&gt;");
        from = k + 1;
      }
      para(words.substr(from));
      x1 = run.x1;
    }
  }

  void end_paragraph() {
    set_style(0);
    // Soft hyphens left at the end of a paragraph mean nothing.
    std::string_view p = trim({para_, para_len_});
    if (!p.empty() && page_ != nullptr) {
      char tag[8];
      std::snprintf(tag, sizeof(tag), "%s>", kind_ == Kind::Heading ? (heading_level_ == 1 ? "h1" : "h2") : "p");
      page_->raw("<");
      page_->raw(tag);
      page_->raw(p);
      page_->raw("</");
      page_->raw(tag);
      page_->raw("\n");
    }
    para_len_ = 0;
    kind_ = Kind::None;
    glue_ = false;
  }

  enum class Kind : uint8_t { None, Paragraph, Heading };
  struct Count {
    float key;
    uint32_t n;
  };

  Job& job_;
  File& f_;
  bool ok_ = false;
  XRef* xref_;
  PageRef* pages_;
  char* slot_[kSlots];
  uint8_t* objstm_;
  uint32_t objstm_num_ = kNoObject;
  uint32_t objstm_first_ = 0;
  uint32_t objstm_count_ = 0;
  uint32_t objstm_len_ = 0;
  uint8_t* content_;
  uint32_t content_used_ = 0;
  uint8_t* chunk_;
  tinfl_decompressor* inflater_;
  uint32_t* pool_;
  uint32_t pool_used_ = 16;  // offset 0 means "none"
  bool pool_full_ = false;
  Font* fonts_;
  int font_count_ = 0;
  FontName font_names_[3][kMaxFontNames];
  Run* runs_;
  int run_count_ = 0;
  Line* lines_;
  char* run_text_;
  uint32_t run_used_ = 0;
  bool space_pending_ = false;
  bool line_break_ = false;
  Outline* outline_;
  int outline_count_ = 0;
  char* para_;
  size_t para_len_ = 0;
  uint8_t style_ = 0;
  Kind kind_ = Kind::None;
  int heading_level_ = 2;
  float heading_size_ = 0;
  float prev_y_ = 0, prev_x0_ = 0;
  bool prev_short_ = true;
  bool prev_end_ = true;    // that line ended a paragraph for sure
  bool prev_stop_ = false;  // it ended a sentence
  float prev_x1_ = 0;
  bool glue_ = false;
  StackItem* stack_;
  uint32_t page_count_ = 0;
  uint32_t root_ = kNoObject;
  uint32_t info_ = kNoObject;
  bool encrypted_ = false;
  char lang_[16] = "";
  float page_y0_ = 0, page_y1_ = 792;
  uint32_t heads_[2] = {0, 0}, feet_[2] = {0, 0};
  bool probe_ = false;  // reading a page only for its running head and foot
  int mark_next_ = 0, mark_end_ = 0;
  int16_t mark_line_[kMaxMarks];
  Count doc_sizes_[32];
  int doc_size_count_ = 0;
  uint64_t chars_ = 0;
  Page* page_ = nullptr;
};

}  // namespace

Result pdf(Job& job) {
  auto* book = new (std::nothrow) Pdf(job);
  if (book == nullptr) return Result::NoMemory;
  const Result r = book->run();
  delete book;
  return r;
}

}  // namespace vellum::convert

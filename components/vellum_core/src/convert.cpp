// Word, Markdown and RTF books into web pages; the formats with more to
// them (Kindle, PDF) are in convert_mobi.cpp and convert_pdf.cpp.
#include <cstdio>
#include <cstring>
#include <new>

#include "convert_internal.h"
#include "vellum/charset.h"
#include "vellum/zip.h"

namespace vellum::convert {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

bool is_alnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || static_cast<uint8_t>(c) >= 0x80;
}

bool same_lower(std::string_view a, std::string_view lower) {
  if (a.size() != lower.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower(a[i]) != lower[i]) return false;
  }
  return true;
}

// Copies s into out (NUL-terminated), cut at a character boundary to fit.
void copy_utf8(std::string_view s, char* out, size_t size) {
  size_t n = std::min<size_t>(s.size(), size - 1);
  if (n < s.size()) {
    while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
  }
  std::memcpy(out, s.data(), n);
  out[n] = '\0';
}

// The path of `src` (relative to the book at `book`) as seen from the web
// page in vellum/converted/: "../../" up to the card's root.
void relative_to_page(const char* book, std::string_view src, char* out, size_t size) {
  const char* slash = std::strrchr(book, '/');
  const int dir = slash == nullptr ? 0 : static_cast<int>(slash - book) + 1;
  std::snprintf(out, size, "../../%.*s%.*s", dir, book, static_cast<int>(src.size()), src.data());
}

// ---- Markdown -----------------------------------------------------------------

// Reads one line (without its line break) into out; false at the end.
bool read_line(Bytes& in, char* out, size_t cap, size_t& len) {
  len = 0;
  int c = in.get();
  if (c < 0) return false;
  for (; c >= 0 && c != '\n'; c = in.get()) {
    if (c == '\r') continue;
    if (len + 1 < cap) out[len++] = static_cast<char>(c);
  }
  out[len] = '\0';
  return true;
}

class Markdown {
 public:
  Markdown(Page& page, const char* book) : page_(page), book_(book) {}

  // Inline text: emphasis, code, links (their words) and pictures.
  void inline_text(std::string_view s) {
    bool bold = false, italic = false;
    for (size_t i = 0; i < s.size();) {
      const char c = s[i];
      if (c == '\\' && i + 1 < s.size() && !is_alnum(s[i + 1]) && !is_space(s[i + 1])) {
        page_.text(s.substr(i + 1, 1));
        i += 2;
        continue;
      }
      if (c == '`') {
        size_t ticks = 0;
        while (i + ticks < s.size() && s[i + ticks] == '`') ++ticks;
        const size_t end = s.find(std::string_view(s.data() + i, ticks), i + ticks);
        if (end != std::string_view::npos) {
          page_.text(trim(s.substr(i + ticks, end - i - ticks)));
          i = end + ticks;
          continue;
        }
        page_.text(s.substr(i, ticks));
        i += ticks;
        continue;
      }
      if (c == '[' || (c == '!' && i + 1 < s.size() && s[i + 1] == '[')) {
        const bool picture = c == '!';
        const size_t open = i + (picture ? 1 : 0);
        const size_t close = s.find(']', open);
        if (close != std::string_view::npos && close + 1 < s.size() && s[close + 1] == '(') {
          const size_t end = s.find(')', close + 2);
          if (end != std::string_view::npos) {
            const std::string_view words = s.substr(open + 1, close - open - 1);
            std::string_view target = trim(s.substr(close + 2, end - close - 2));
            if (const size_t sp = target.find(' '); sp != std::string_view::npos) target = target.substr(0, sp);
            if (picture) {
              char src[200];
              relative_to_page(book_, target, src, sizeof(src));
              page_.raw("<img src=\"");
              if (target.find("://") == std::string_view::npos) page_.text(src);
              page_.raw("\" alt=\"");
              page_.text(words);
              page_.raw("\"/>");
            } else {
              inline_text(words);
            }
            i = end + 1;
            continue;
          }
        }
      }
      if (c == '<') {
        const size_t end = s.find('>', i);
        const std::string_view inner =
            end == std::string_view::npos ? std::string_view{} : s.substr(i + 1, end - i - 1);
        if (inner.find("://") != std::string_view::npos || inner.find('@') != std::string_view::npos) {
          page_.text(inner);  // an address shown as it is
          i = end + 1;
          continue;
        }
        if (!inner.empty() && (inner[0] == '/' || is_alnum(inner[0]))) {
          // Inline HTML (<br>, <sup>...): kept as written.
          page_.raw(s.substr(i, end - i + 1));
          i = end + 1;
          continue;
        }
      }
      if (c == '*' || c == '_') {
        size_t run = 0;
        while (i + run < s.size() && s[i + run] == c) ++run;
        const char before = i > 0 ? s[i - 1] : ' ';
        const char after = i + run < s.size() ? s[i + run] : ' ';
        // "_" inside a word (snake_case) is a letter; a mark needs a word on one side.
        const bool inside = c == '_' && is_alnum(before) && is_alnum(after);
        const bool opens = !is_space(after);
        const bool closes = !is_space(before);
        if (!inside && run <= 3 && (opens || closes)) {
          auto close_italic = [&] {
            page_.raw("</i>");
            italic = false;
          };
          auto close_bold = [&] {
            // Italics sit inside bold: closed and opened again around it.
            if (italic) page_.raw("</i>");
            page_.raw("</b>");
            if (italic) page_.raw("<i>");
            bold = false;
          };
          if (run == 3 && (bold || italic) && closes) {
            if (italic) close_italic();
            if (bold) close_bold();
          } else if (run == 3 && opens) {
            page_.raw("<b><i>");
            bold = italic = true;
          } else if (run == 2 && bold && closes) {
            close_bold();
          } else if (run == 2 && !bold && opens) {
            page_.raw("<b>");
            bold = true;
          } else if (run == 1 && italic && closes) {
            close_italic();
          } else if (run == 1 && !italic && opens) {
            page_.raw("<i>");
            italic = true;
          } else {
            page_.text(s.substr(i, run));
          }
          i += run;
          continue;
        }
        page_.text(s.substr(i, run));
        i += run;
        continue;
      }
      // Plain text up to the next character that may mean something.
      size_t end = i + 1;
      while (end < s.size() && std::strchr("\\`[!<*_", s[end]) == nullptr) ++end;
      page_.text(s.substr(i, end - i));
      i = end;
    }
    if (italic) page_.raw("</i>");
    if (bold) page_.raw("</b>");
  }

  void flush() {
    if (para_len_ == 0) return;
    page_.raw("<p>");
    inline_text({para_, para_len_});
    page_.raw("</p>\n");
    para_len_ = 0;
  }

  void add_to_paragraph(std::string_view line) {
    line = trim(line);
    if (line.empty()) return;
    // A hard break: two spaces or a backslash at the end of the line.
    if (para_len_ > 0 && para_len_ + 1 < sizeof(para_)) para_[para_len_++] = ' ';
    const size_t n = std::min<size_t>(line.size(), sizeof(para_) - 1 - para_len_);
    std::memcpy(para_ + para_len_, line.data(), n);
    para_len_ += n;
  }

  void close_blocks(bool lists, bool quote, bool table) {
    flush();
    if (lists && list_ != 0) {
      page_.raw(list_ == 1 ? "</ul>\n" : "</ol>\n");
      list_ = 0;
    }
    if (quote && quote_) {
      page_.raw("</blockquote>\n");
      quote_ = false;
    }
    if (table && table_) {
      page_.raw("</table>\n");
      table_ = false;
    }
  }

  void line(std::string_view raw) {
    std::string_view s = raw;
    // Fenced code: every line as it is.
    const std::string_view t = trim(s);
    if (t.starts_with("```") || t.starts_with("~~~")) {
      close_blocks(true, false, true);
      fence_ = !fence_;
      return;
    }
    if (fence_) {
      page_.raw("<p class=\"code\">");
      page_.text(s);
      page_.raw("</p>\n");
      return;
    }
    if (t.empty()) {
      flush();
      blank_ = true;
      return;
    }
    // Quotations: "> text".
    const bool quoted = t[0] == '>';
    if (quoted) {
      s = t.substr(1);
      if (!s.empty() && s[0] == ' ') s.remove_prefix(1);
      if (!quote_) {
        close_blocks(true, false, true);
        page_.raw("<blockquote>\n");
        quote_ = true;
      }
    } else if (quote_ && blank_) {
      close_blocks(true, true, true);
    }
    const std::string_view st = trim(s);
    blank_ = false;
    if (st.empty()) {
      flush();
      return;
    }

    // Setext headings: a line of "=" or "-" under a one-line paragraph.
    if (para_len_ > 0 && !paragraph_lines_more_than_one_ &&
        st.find_first_not_of(st[0] == '=' ? '=' : '-') == std::string_view::npos && (st[0] == '=' || st[0] == '-') &&
        list_ == 0) {
      const int level = st[0] == '=' ? 1 : 2;
      char tag[8];
      std::snprintf(tag, sizeof(tag), "h%d>", level);
      page_.raw("<");
      page_.raw(tag);
      inline_text({para_, para_len_});
      page_.raw("</");
      page_.raw(tag);
      page_.raw("\n");
      para_len_ = 0;
      return;
    }
    // Rules: three or more of "-", "*" or "_" (spaces allowed).
    {
      int marks = 0;
      bool only = true;
      for (char c : st) {
        if (c == st[0]) {
          ++marks;
        } else if (c != ' ') {
          only = false;
        }
      }
      if (only && marks >= 3 && (st[0] == '-' || st[0] == '*' || st[0] == '_')) {
        close_blocks(true, false, true);
        page_.raw("<hr/>\n");
        return;
      }
    }
    // ATX headings.
    if (st[0] == '#') {
      size_t level = 0;
      while (level < st.size() && st[level] == '#') ++level;
      if (level <= 6 && (level == st.size() || st[level] == ' ')) {
        close_blocks(true, true, true);
        std::string_view words = trim(st.substr(level));
        while (!words.empty() && words.back() == '#') words.remove_suffix(1);
        char tag[8];
        std::snprintf(tag, sizeof(tag), "h%d>", static_cast<int>(level));
        page_.raw("<");
        page_.raw(tag);
        inline_text(trim(words));
        page_.raw("</");
        page_.raw(tag);
        page_.raw("\n");
        return;
      }
    }
    // Tables: "| a | b |" rows; the "|---|" line under the head is left out.
    if (st[0] == '|') {
      if (st.find_first_not_of("|-: ") == std::string_view::npos) return;
      if (!table_) {
        close_blocks(true, false, false);
        page_.raw("<table>\n");
        table_ = true;
      }
      page_.raw("<tr>");
      std::string_view cells = st.substr(1);
      if (!cells.empty() && cells.back() == '|') cells.remove_suffix(1);
      while (!cells.empty()) {
        const size_t bar = cells.find('|');
        page_.raw("<td>");
        inline_text(trim(cells.substr(0, bar)));
        page_.raw("</td>");
        if (bar == std::string_view::npos) break;
        cells.remove_prefix(bar + 1);
      }
      page_.raw("</tr>\n");
      return;
    }
    if (table_) close_blocks(false, false, true);
    // List items: "- ", "* ", "+ " or "1. ".
    {
      size_t k = 0;
      int kind = 0;
      if ((st[0] == '-' || st[0] == '*' || st[0] == '+') && st.size() > 1 && st[1] == ' ') {
        kind = 1;
        k = 2;
      } else {
        while (k < st.size() && st[k] >= '0' && st[k] <= '9') ++k;
        if (k > 0 && k < 10 && k + 1 < st.size() && (st[k] == '.' || st[k] == ')') && st[k + 1] == ' ') {
          kind = 2;
          k += 2;
        }
      }
      if (kind != 0) {
        flush();
        if (list_ != kind) {
          close_blocks(true, false, false);
          page_.raw(kind == 1 ? "<ul>\n" : "<ol>\n");
          list_ = kind;
        }
        page_.raw("<li>");
        inline_text(trim(st.substr(k)));
        page_.raw("</li>\n");
        return;
      }
    }
    // Raw HTML blocks are kept as they are.
    if (st[0] == '<' && st.size() > 1 && (st[1] == '/' || is_alnum(st[1]) || st[1] == '!')) {
      close_blocks(true, false, true);
      page_.raw(st);
      page_.raw("\n");
      return;
    }
    // A line after a list item continues it only when indented; else the list ends.
    if (list_ != 0 && s.size() > 0 && s[0] != ' ' && s[0] != '\t') close_blocks(true, false, false);
    paragraph_lines_more_than_one_ = para_len_ > 0;
    add_to_paragraph(st);
  }

  void end() { close_blocks(true, true, true); }

 private:
  Page& page_;
  const char* book_;
  char para_[16384];
  size_t para_len_ = 0;
  bool paragraph_lines_more_than_one_ = false;
  int list_ = 0;  // 1 bullets, 2 numbers
  bool quote_ = false;
  bool table_ = false;
  bool fence_ = false;
  bool blank_ = true;
};

// ---- RTF ------------------------------------------------------------------------

class Rtf {
 public:
  Rtf(Page& page, Bytes& in) : page_(page), in_(in) {}

  void run() {
    while (!in_.failed()) {
      const int c = in_.get();
      if (c < 0) break;
      if (c == '{') {
        if (depth_ + 1 < kMaxDepth) {
          stack_[depth_ + 1] = stack_[depth_];
          ++depth_;
        } else {
          ++overflow_;
        }
        continue;
      }
      if (c == '}') {
        if (overflow_ > 0) {
          --overflow_;
        } else if (depth_ > 0) {
          end_group();
          --depth_;
        }
        continue;
      }
      if (c == '\\') {
        control();
        continue;
      }
      if (c == '\r' || c == '\n') continue;
      if (pending_skip_ > 0) {
        --pending_skip_;
        continue;
      }
      put(static_cast<uint32_t>(c) < 0x80 ? static_cast<uint32_t>(c)
                                          : charset::code_point(enc_, static_cast<uint8_t>(c)));
    }
    end_paragraph();
    start_page();
  }

  bool wrote_text() const { return text_ > 0; }

 private:
  enum class Dest : uint8_t { Text, Skip, Title, Author, StyleName };
  struct State {
    Dest dest = Dest::Text;
    bool bold = false;
    bool italic = false;
    uint8_t uc = 1;
  };
  static constexpr int kMaxDepth = 64;

  State& top() { return stack_[depth_]; }

  // A control word or symbol, after its backslash.
  void control() {
    int c = in_.get();
    if (c < 0) return;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
      if (c == '\'') {
        const int h = hex(in_.get()), l = hex(in_.get());
        if (h < 0 || l < 0) return;
        if (pending_skip_ > 0) {
          --pending_skip_;
          return;
        }
        put(charset::code_point(enc_, static_cast<uint8_t>(h << 4 | l)));
      } else if (c == '*') {
        top().dest = Dest::Skip;
      } else if (c == '\\' || c == '{' || c == '}') {
        put(static_cast<uint32_t>(c));
      } else if (c == '~') {
        put(0xA0);
      } else if (c == '_') {
        put(0x2011);
      } else if (c == '\n' || c == '\r') {
        end_paragraph();
      }
      return;
    }
    char word[32];
    size_t n = 0;
    while ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      if (n + 1 < sizeof(word)) word[n++] = static_cast<char>(c);
      c = in_.get();
    }
    word[n] = '\0';
    bool has_num = false, neg = false;
    int32_t num = 0;
    if (c == '-') {
      neg = true;
      c = in_.get();
    }
    while (c >= '0' && c <= '9') {
      has_num = true;
      num = num * 10 + (c - '0');
      if (num > 1000000) num = 1000000;
      c = in_.get();
    }
    if (neg) num = -num;
    handle(word, has_num, num);
    // The space after a control word belongs to it; anything else is text.
    if (c >= 0 && c != ' ') reprocess(c);
  }

  void reprocess(int c) {
    if (c == '\\') {
      control();
    } else if (c == '{') {
      if (depth_ + 1 < kMaxDepth) {
        stack_[depth_ + 1] = stack_[depth_];
        ++depth_;
      } else {
        ++overflow_;
      }
    } else if (c == '}') {
      if (overflow_ > 0) {
        --overflow_;
      } else if (depth_ > 0) {
        end_group();
        --depth_;
      }
    } else if (c != '\r' && c != '\n') {
      if (pending_skip_ > 0) {
        --pending_skip_;
        return;
      }
      put(static_cast<uint32_t>(c) < 0x80 ? static_cast<uint32_t>(c)
                                          : charset::code_point(enc_, static_cast<uint8_t>(c)));
    }
  }

  static int hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  void handle(std::string_view w, bool has_num, int32_t num) {
    State& s = top();
    static constexpr const char* kHidden[] = {"fonttbl",
                                              "colortbl",
                                              "pict",
                                              "header",
                                              "headerl",
                                              "headerr",
                                              "headerf",
                                              "footer",
                                              "footerl",
                                              "footerr",
                                              "footerf",
                                              "listtable",
                                              "listoverridetable",
                                              "rsidtbl",
                                              "generator",
                                              "xmlnstbl",
                                              "themedata",
                                              "colorschememapping",
                                              "latentstyles",
                                              "datastore",
                                              "object",
                                              "fldinst",
                                              "footnote",
                                              "annotation",
                                              "filetbl",
                                              "revtbl",
                                              "pgdsctbl",
                                              "operator",
                                              "keywords",
                                              "subject",
                                              "doccomm",
                                              "company",
                                              "category",
                                              "comment",
                                              "manager",
                                              "hlinkbase",
                                              "nonshppict",
                                              "shp",
                                              "bkmkstart",
                                              "bkmkend",
                                              "template",
                                              "userprops",
                                              "docvar",
                                              "mmathPr",
                                              "wgrffmtfilter",
                                              "fchars",
                                              "lchars",
                                              "pnseclvl",
                                              "pntext",
                                              "pntxta",
                                              "pntxtb",
                                              "listtext",
                                              "do",
                                              "background",
                                              "atnid",
                                              "atnauthor"};
    if (w == "info") {
      in_info_ = true;
      s.dest = Dest::Skip;
      info_depth_ = depth_;
      return;
    }
    if (in_info_ && (w == "title" || w == "author")) {
      s.dest = w == "title" ? Dest::Title : Dest::Author;
      return;
    }
    if (w == "stylesheet") {
      in_styles_ = true;
      styles_depth_ = depth_;
      s.dest = Dest::Skip;
      return;
    }
    if (in_styles_ && w == "s" && depth_ > styles_depth_) {
      style_ = has_num ? num : 0;
      style_name_len_ = 0;
      s.dest = Dest::StyleName;
      return;
    }
    for (const char* h : kHidden) {
      if (w == h) {
        s.dest = Dest::Skip;
        return;
      }
    }
    if (w == "ansicpg") {
      enc_ = num == 1257   ? charset::Encoding::Windows1257
             : num == 1251 ? charset::Encoding::Windows1251
                           : charset::Encoding::Windows1252;
    } else if (w == "uc") {
      s.uc = static_cast<uint8_t>(num < 0 ? 0 : num > 8 ? 8 : num);
    } else if (w == "u") {
      uint32_t cp = static_cast<uint32_t>(num < 0 ? num + 65536 : num);
      if (cp >= 0xD800 && cp < 0xDC00) {
        high_ = cp;
        cp = 0;
      } else if (cp >= 0xDC00 && cp < 0xE000 && high_ != 0) {
        cp = 0x10000 + ((high_ - 0xD800) << 10) + (cp - 0xDC00);
        high_ = 0;
      }
      if (cp != 0) put(cp);
      pending_skip_ = s.uc;
      return;
    } else if (w == "par" || w == "sect" || w == "page") {
      end_paragraph();
    } else if (w == "pard") {
      level_ = 0;
    } else if (w == "s" && s.dest == Dest::Text) {
      level_ = has_num && num >= 0 && num < 256 ? style_level_[num] : 0;
    } else if (w == "outlinelevel") {
      level_ = has_num && num >= 0 && num < 6 ? num + 1 : 0;
    } else if (w == "line") {
      if (s.dest == Dest::Text) raw_para("<br/>");
    } else if (w == "tab") {
      put(' ');
    } else if (w == "cell") {
      put(' ');
    } else if (w == "row") {
      end_paragraph();
    } else if (w == "b") {
      s.bold = !has_num || num != 0;
    } else if (w == "i") {
      s.italic = !has_num || num != 0;
    } else if (w == "plain") {
      s.bold = s.italic = false;
    } else if (w == "emdash") {
      put(0x2014);
    } else if (w == "endash") {
      put(0x2013);
    } else if (w == "lquote") {
      put(0x2018);
    } else if (w == "rquote") {
      put(0x2019);
    } else if (w == "ldblquote") {
      put(0x201C);
    } else if (w == "rdblquote") {
      put(0x201D);
    } else if (w == "bullet") {
      put(0x2022);
    } else if (w == "emspace" || w == "enspace" || w == "qmspace") {
      put(' ');
    }
  }

  void end_group() {
    if (in_styles_ && stack_[depth_].dest == Dest::StyleName) {
      // "heading 1;" names a heading style; "Title;" one too.
      std::string_view name = trim({style_name_, style_name_len_});
      if (!name.empty() && name.back() == ';') name = trim(name.substr(0, name.size() - 1));
      int level = 0;
      char lower[64];
      const size_t n = std::min<size_t>(name.size(), sizeof(lower));
      for (size_t k = 0; k < n; ++k) lower[k] = ascii_lower(name[k]);
      const std::string_view l(lower, n);
      if (l == "title") level = 1;
      if (l.size() == 9 && l.starts_with("heading ") && l[8] >= '1' && l[8] <= '6') level = l[8] - '0';
      if (style_ >= 0 && style_ < 256) style_level_[style_] = static_cast<uint8_t>(level);
      style_name_len_ = 0;
    }
    if (in_styles_ && depth_ == styles_depth_) in_styles_ = false;
    if (in_info_ && depth_ == info_depth_) in_info_ = false;
  }

  void put(uint32_t cp) {
    State& s = top();
    switch (s.dest) {
      case Dest::Skip:
        return;
      case Dest::Title:
      case Dest::Author: {
        char* out = s.dest == Dest::Title ? title_ : author_;
        size_t& len = s.dest == Dest::Title ? title_len_ : author_len_;
        if (len + 5 < sizeof(title_)) len += xml::encode_utf8(cp, out + len);
        return;
      }
      case Dest::StyleName:
        if (style_name_len_ + 5 < sizeof(style_name_))
          style_name_len_ += xml::encode_utf8(cp, style_name_ + style_name_len_);
        return;
      case Dest::Text:
        break;
    }
    if (cp < 0x20 && cp != '\t') return;
    // Emphasis follows the group the text is in.
    if (s.bold != bold_ || s.italic != italic_) {
      if (italic_) raw_para("</i>");
      if (bold_) raw_para("</b>");
      if (s.bold) raw_para("<b>");
      if (s.italic) raw_para("<i>");
      bold_ = s.bold;
      italic_ = s.italic;
    }
    if (cp != ' ' && cp != '\t' && cp != 0xA0) has_words_ = true;
    char u[8];
    const char* esc = cp == '&' ? "&amp;" : cp == '<' ? "&lt;" : cp == '>' ? "&gt;" : nullptr;
    if (esc != nullptr) {
      raw_para(esc);
    } else {
      raw_para({u, xml::encode_utf8(cp == '\t' ? ' ' : cp, u)});
    }
  }

  void raw_para(std::string_view s) {
    const size_t n = std::min<size_t>(s.size(), sizeof(para_) - 16 - para_len_);
    std::memcpy(para_ + para_len_, s.data(), n);
    para_len_ += n;
  }

  void start_page() {
    if (started_) return;
    started_ = true;
    page_.start(trim({title_, title_len_}), trim({author_, author_len_}), "", "", false);
  }

  void end_paragraph() {
    if (italic_) raw_para("</i>");
    if (bold_) raw_para("</b>");
    bold_ = italic_ = false;
    if (has_words_) {
      start_page();
      char tag[8];
      std::snprintf(tag, sizeof(tag), "%s>", level_ > 0 ? (level_ == 1 ? "h1" : level_ == 2 ? "h2" : "h3") : "p");
      page_.raw("<");
      page_.raw(tag);
      page_.raw({para_, para_len_});
      page_.raw("</");
      page_.raw(tag);
      page_.raw("\n");
      ++text_;
    }
    para_len_ = 0;
    has_words_ = false;
  }

  Page& page_;
  Bytes& in_;
  State stack_[kMaxDepth];
  int depth_ = 0;
  int overflow_ = 0;
  charset::Encoding enc_ = charset::Encoding::Windows1252;
  uint32_t pending_skip_ = 0;
  uint32_t high_ = 0;
  bool in_info_ = false, in_styles_ = false;
  int info_depth_ = 0, styles_depth_ = 0;
  int32_t style_ = 0;
  char style_name_[64];
  size_t style_name_len_ = 0;
  uint8_t style_level_[256] = {};
  int level_ = 0;
  char title_[200];
  size_t title_len_ = 0;
  char author_[200];
  size_t author_len_ = 0;
  char para_[32768];
  size_t para_len_ = 0;
  bool bold_ = false, italic_ = false, has_words_ = false;
  bool started_ = false;
  uint32_t text_ = 0;
};

// ---- Word -----------------------------------------------------------------------

struct StyleLevel {
  char id[40];
  uint8_t level;     // 1-6 for headings
  uint8_t emphasis;  // character styles: 1 bold, 2 italic
};

// Heading and emphasis styles from word/styles.xml: style id to heading
// level (1-6) or bold and italic, and the document's language from its
// defaults.
int read_styles(std::string_view xml, StyleLevel* out, int max, char* language, size_t lang_size) {
  int count = 0;
  size_t pos = 0, start = 0;
  xml::Tag tag;
  char id[40] = "";
  int level = 0, emphasis = 0;
  bool in_style = false, defaults = false;
  auto end_style = [&] {
    if (in_style && (level > 0 || emphasis > 0) && id[0] != '\0' && count < max) {
      std::memcpy(out[count].id, id, sizeof(id));
      out[count].level = static_cast<uint8_t>(level);
      out[count++].emphasis = static_cast<uint8_t>(emphasis);
    }
    in_style = false;
  };
  while (xml::next_tag(xml, pos, tag, start)) {
    if (xml::name_is(tag.name, "docdefaults")) {
      defaults = !tag.closing;
    } else if (xml::name_is(tag.name, "lang") && defaults && language[0] == '\0') {
      const std::string_view v = xml::attr(tag.attrs, "val");
      copy_utf8(v.substr(0, v.find('-')), language, lang_size);
    } else if (xml::name_is(tag.name, "style")) {
      if (tag.closing) {
        end_style();
      } else {
        in_style = true;
        level = emphasis = 0;
        copy_utf8(xml::attr(tag.attrs, "styleId"), id, sizeof(id));
        if (tag.self_closing) end_style();
      }
    } else if (in_style && xml::name_is(tag.name, "name")) {
      const std::string_view v = xml::attr(tag.attrs, "val");
      if (same_lower(v, "title")) level = 1;
      if (v.size() == 9 && same_lower(v.substr(0, 8), "heading ") && v[8] >= '1' && v[8] <= '6') level = v[8] - '0';
    } else if (in_style && xml::name_is(tag.name, "outlinelvl")) {
      const std::string_view v = xml::attr(tag.attrs, "val");
      if (v.size() == 1 && v[0] >= '0' && v[0] <= '5') level = v[0] - '0' + 1;
    } else if (in_style && (xml::name_is(tag.name, "b") || xml::name_is(tag.name, "i")) && !tag.closing) {
      const std::string_view v = xml::attr(tag.attrs, "val");
      if (v.empty() || !(v == "0" || v == "false" || v == "off")) emphasis |= xml::name_is(tag.name, "b") ? 1 : 2;
    }
  }
  return count;
}

struct Rel {
  char id[24];
  char target[80];
};

}  // namespace

Kind kind_of(const char* path) {
  const std::string_view p(path);
  if (ends_with_lower(p, ".mobi") || ends_with_lower(p, ".azw3") || ends_with_lower(p, ".azw") ||
      ends_with_lower(p, ".prc") || ends_with_lower(p, ".pdb")) {
    return Kind::Mobi;
  }
  if (ends_with_lower(p, ".pdf")) return Kind::Pdf;
  if (ends_with_lower(p, ".docx")) return Kind::Docx;
  if (ends_with_lower(p, ".md") || ends_with_lower(p, ".markdown")) return Kind::Markdown;
  if (ends_with_lower(p, ".rtf")) return Kind::Rtf;
  return Kind::None;
}

Result run(Kind kind, Job& job) {
  if (job.storage == nullptr || job.in == nullptr || job.work == nullptr || job.work_size < kWorkBytes) {
    return Result::NoMemory;
  }
  switch (kind) {
    case Kind::Mobi:
      return mobi(job);
    case Kind::Pdf:
      return pdf(job);
    case Kind::Docx:
      return docx(job);
    case Kind::Markdown:
      return markdown(job);
    case Kind::Rtf:
      return rtf(job);
    case Kind::None:
      break;
  }
  return Result::Unreadable;
}

Result markdown(Job& job) {
  File& f = *job.in;
  // Work memory: the reading window, a line, the line in UTF-8, the converter.
  uint8_t* window = job.work;
  constexpr uint32_t kWindow = 64 * 1024;
  char* line = reinterpret_cast<char*>(job.work + kWindow);
  constexpr size_t kLine = 16 * 1024;
  char* utf8 = line + kLine;
  static_assert(sizeof(Markdown) < 64 * 1024);
  auto* md_mem = job.work + kWindow + kLine + 3 * kLine;

  const uint32_t head = std::min<uint32_t>(f.size(), 4096);
  if (head > 0 && f.read_at(0, window, head) != static_cast<int32_t>(head)) return Result::Unreadable;
  uint32_t bom = 0;
  const charset::Encoding enc = charset::detect(window, head, bom);
  if (charset::unit(enc) != 1) return Result::Unreadable;
  auto to_utf8 = [&](size_t len) -> std::string_view {
    if (enc == charset::Encoding::Utf8) return {line, len};
    return {utf8, charset::to_utf8(enc, reinterpret_cast<const uint8_t*>(line), static_cast<uint32_t>(len), utf8,
                                   static_cast<uint32_t>(3 * kLine))};
  };

  // The title: front matter's "title:", else the first heading.
  char title[160] = "", author[160] = "";
  uint32_t body = bom;
  {
    Bytes in(f, window, kWindow, bom);
    size_t len;
    int n = 0;
    bool front = false;
    while (n++ < 400 && read_line(in, line, kLine, len)) {
      std::string_view s = trim(to_utf8(len));
      if (n == 1 && s == "---") {
        front = true;
        continue;
      }
      if (front) {
        if (s == "---" || s == "...") {
          front = false;
          body = in.pos();
          continue;
        }
        for (auto [key, out] : {std::pair<const char*, char*>{"title:", title}, {"author:", author}}) {
          if (s.starts_with(key)) {
            std::string_view v = trim(s.substr(std::strlen(key)));
            if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
            copy_utf8(v, out, sizeof(title));
          }
        }
        continue;
      }
      if (title[0] == '\0' && s.starts_with("#")) {
        size_t k = 0;
        while (k < s.size() && s[k] == '#') ++k;
        if (k < s.size() && s[k] == ' ') {
          std::string_view words = trim(s.substr(k));
          while (!words.empty() && words.back() == '#') words.remove_suffix(1);
          copy_utf8(trim(words), title, sizeof(title));
        }
      }
      if (title[0] != '\0') break;
    }
  }

  Page page(job);
  if (!page.ok()) return Result::Unreadable;
  page.start(title, author, "", "", false);
  auto* md = new (md_mem) Markdown(page, job.in_path);
  Bytes in(f, window, kWindow, body);
  size_t len;
  while (read_line(in, line, kLine, len)) md->line(to_utf8(len));
  md->end();
  md->~Markdown();
  page.end();
  if (in.failed()) return Result::Unreadable;
  return page.finish(true) ? Result::Ok : Result::Unreadable;
}

Result rtf(Job& job) {
  File& f = *job.in;
  uint8_t head[6] = {};
  if (f.read_at(0, head, 5) != 5 || std::memcmp(head, "{\\rtf", 5) != 0) return Result::Unreadable;
  constexpr uint32_t kWindow = 64 * 1024;
  static_assert(sizeof(Rtf) < 128 * 1024);
  Page page(job);
  if (!page.ok()) return Result::Unreadable;
  Bytes in(f, job.work, kWindow);
  auto* r = new (job.work + kWindow) Rtf(page, in);
  r->run();
  const bool any = r->wrote_text();
  r->~Rtf();
  page.end();
  if (in.failed() || !any) return Result::Unreadable;
  return page.finish(true) ? Result::Ok : Result::Unreadable;
}

Result docx(Job& job) {
  if (job.zip == nullptr) return Result::NoMemory;
  Zip& zip = *job.zip;
  Storage& storage = *job.storage;
  if (!zip.open(job.in)) return Result::Unreadable;
  const int document = zip.find("word/document.xml");
  if (document < 0) {
    zip.close();
    return Result::Unreadable;
  }
  // Work memory: small files read whole, the reading window, a paragraph.
  uint8_t* small = job.work;  // styles, core properties, relationships, pictures
  constexpr uint32_t kSmall = 1024 * 1024;
  uint8_t* window = job.work + kSmall;
  constexpr uint32_t kWindow = 128 * 1024;
  char* para = reinterpret_cast<char*>(window + kWindow);
  constexpr size_t kPara = 64 * 1024;
  auto* styles = reinterpret_cast<StyleLevel*>(para + kPara);
  constexpr int kMaxStyles = 256;
  auto* rels = reinterpret_cast<Rel*>(styles + kMaxStyles);
  constexpr int kMaxRels = 512;
  static_assert(kSmall + kWindow + kPara + kMaxStyles * sizeof(StyleLevel) + kMaxRels * sizeof(Rel) <= kWorkBytes);

  auto read_small = [&](const char* name) -> std::string_view {
    const int i = zip.find(name);
    if (i < 0 || zip.size(i) >= kSmall) return {};
    const int32_t n = zip.extract(i, small, kSmall - 1);
    return n > 0 ? std::string_view(reinterpret_cast<char*>(small), static_cast<size_t>(n)) : std::string_view{};
  };

  // Title and author.
  char title[160] = "", author[160] = "", language[16] = "";
  {
    const std::string_view core = read_small("docProps/core.xml");
    size_t pos = 0, start = 0, before = 0;
    xml::Tag tag;
    while (xml::next_tag(core, pos, tag, start)) {
      const std::string_view text = core.substr(before, start - before);
      before = pos;
      if (!tag.closing) continue;
      char* out = xml::name_is(tag.name, "title")      ? title
                  : xml::name_is(tag.name, "creator")  ? author
                  : xml::name_is(tag.name, "language") ? language
                                                       : nullptr;
      if (out == nullptr || out[0] != '\0') continue;
      char buf[160];
      const size_t n = append_xml_text(text, buf, 0, sizeof(buf));
      copy_utf8(trim({buf, n}), out, out == language ? sizeof(language) : sizeof(title));
    }
    if (char* dash = std::strchr(language, '-')) *dash = '\0';
  }
  char style_lang[16] = "";
  const int style_count =
      read_styles(read_small("word/styles.xml"), styles, kMaxStyles, style_lang, sizeof(style_lang));
  if (language[0] == '\0') std::memcpy(language, style_lang, sizeof(language));
  int rel_count = 0;
  {
    const std::string_view x = read_small("word/_rels/document.xml.rels");
    size_t pos = 0, start = 0;
    xml::Tag tag;
    while (xml::next_tag(x, pos, tag, start) && rel_count < kMaxRels) {
      if (!xml::name_is(tag.name, "relationship")) continue;
      Rel& r = rels[rel_count];
      copy_utf8(xml::attr(tag.attrs, "Id"), r.id, sizeof(r.id));
      copy_utf8(xml::attr(tag.attrs, "Target"), r.target, sizeof(r.target));
      ++rel_count;
    }
  }

  // The document itself can be many megabytes: unpacked to the card, then
  // read a window at a time.
  bool unpacked = false;
  if (Writer* w = storage.create(job.scratch_path)) {
    const bool done = zip.extract_to(document, *w, window);  // the window doubles as the inflater's
    unpacked = storage.finish(w, done) && done;
  }
  File* doc = unpacked ? storage.open(job.scratch_path) : nullptr;
  if (doc == nullptr) {
    zip.close();
    return Result::Unreadable;
  }

  Page page(job);
  bool ok = page.ok();
  if (ok) page.start(title, author, language, "", false);
  size_t para_len = 0;
  bool in_p = false, in_r = false, in_rpr = false, in_t = false, run_b = false, run_i = false;
  bool cur_b = false, cur_i = false, item = false, any = false, field_code = false;
  int level = 0;
  int pictures = 0;
  auto raw = [&](std::string_view s) {
    const size_t n = std::min<size_t>(s.size(), kPara - 16 - para_len);
    std::memcpy(para + para_len, s.data(), n);
    para_len += n;
  };
  auto raw_char = [&](char c) {
    raw(c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string_view(&c, 1));
  };
  auto set_style = [&](bool b, bool i) {
    if (b == cur_b && i == cur_i) return;
    if (cur_i) raw("</i>");
    if (cur_b) raw("</b>");
    if (b) raw("<b>");
    if (i) raw("<i>");
    cur_b = b;
    cur_i = i;
  };
  auto end_paragraph = [&] {
    set_style(false, false);
    bool words = false;
    for (size_t k = 0; k < para_len && !words; ++k) {
      if (para[k] == '<') {
        while (k < para_len && para[k] != '>') ++k;
        // A picture counts as words.
        words = k >= 4 && std::string_view(para, k).rfind("<img") != std::string_view::npos;
      } else {
        words = !is_space(para[k]);
      }
    }
    if (words) {
      char tag[8];
      std::snprintf(tag, sizeof(tag), "%s>",
                    level > 0 ? (level == 1   ? "h1"
                                 : level == 2 ? "h2"
                                              : "h3")
                    : item    ? "li"
                              : "p");
      if (item && level == 0) page.raw("<ul>");
      page.raw("<");
      page.raw(tag);
      page.raw({para, para_len});
      page.raw("</");
      page.raw(tag);
      if (item && level == 0) page.raw("</ul>");
      page.raw("\n");
      any = true;
    }
    para_len = 0;
  };
  auto picture = [&](std::string_view rid) {
    for (int k = 0; k < rel_count; ++k) {
      if (rid != rels[k].id) continue;
      char name[96];
      std::snprintf(name, sizeof(name), "word/%s", rels[k].target);
      const int entry = zip.find(name);
      if (entry < 0 || zip.size(entry) > kSmall) return;
      const int32_t n = zip.extract(entry, small, kSmall);
      const char* ext = n > 0 ? picture_ext(small, static_cast<uint32_t>(n)) : nullptr;
      if (ext == nullptr) return;
      char path[160];
      std::snprintf(path, sizeof(path), "%s%d.%s", job.pictures, pictures++, ext);
      if (!storage.write(path, small, static_cast<uint32_t>(n))) return;
      const char* file = std::strrchr(path, '/');
      raw("<img src=\"");
      raw(file != nullptr ? file + 1 : path);
      raw("\"/>");
      return;
    }
  };

  const bool walked =
      ok && walk_tags(*doc, 0, window, kWindow, [&](const xml::Tag& tag, std::string_view text, uint32_t, uint32_t) {
        const std::string_view n = tag.name;
        if (in_t && tag.closing && xml::name_is(n, "t")) {
          if (!field_code) {
            set_style(run_b, run_i);
            for (size_t k = 0; k < text.size();) {
              char u[8];
              size_t len = 0;
              if (text[k] == '&' && xml::decode_entity(text, k, u, len)) {
                for (size_t q = 0; q < len; ++q) raw_char(u[q]);
              } else {
                raw_char(text[k++]);
              }
            }
          }
          in_t = false;
          return true;
        }
        if (n.empty()) return true;
        if (xml::name_is(n, "p")) {
          if (tag.closing || tag.self_closing) {
            if (in_p || tag.self_closing) end_paragraph();
            in_p = false;
          } else {
            in_p = true;
            para_len = 0;
            level = 0;
            item = false;
            cur_b = cur_i = false;
          }
        } else if (xml::name_is(n, "r")) {
          in_r = !tag.closing && !tag.self_closing;
          run_b = run_i = false;
        } else if (xml::name_is(n, "rpr")) {
          in_rpr = !tag.closing && !tag.self_closing;
        } else if (in_r && in_rpr && (xml::name_is(n, "b") || xml::name_is(n, "i"))) {
          const std::string_view v = xml::attr(tag.attrs, "val");
          const bool on = v.empty() || !(v == "0" || v == "false" || v == "off");
          (xml::name_is(n, "b") ? run_b : run_i) = on;
        } else if (xml::name_is(n, "t") && !tag.closing && !tag.self_closing) {
          in_t = true;
        } else if (in_r && in_rpr && xml::name_is(n, "rstyle")) {
          const std::string_view v = xml::attr(tag.attrs, "val");
          for (int k = 0; k < style_count; ++k) {
            if (v != styles[k].id) continue;
            run_b |= (styles[k].emphasis & 1) != 0;
            run_i |= (styles[k].emphasis & 2) != 0;
          }
        } else if (xml::name_is(n, "pstyle")) {
          const std::string_view v = xml::attr(tag.attrs, "val");
          for (int k = 0; k < style_count; ++k) {
            if (v == styles[k].id) level = styles[k].level;
          }
        } else if (xml::name_is(n, "outlinelvl")) {
          const std::string_view v = xml::attr(tag.attrs, "val");
          if (v.size() == 1 && v[0] >= '0' && v[0] <= '5') level = v[0] - '0' + 1;
        } else if (xml::name_is(n, "numpr")) {
          item = true;
        } else if ((xml::name_is(n, "tab") && in_r) || xml::name_is(n, "nobreakhyphen")) {
          raw(xml::name_is(n, "tab") ? " " : "-");
        } else if ((xml::name_is(n, "br") || xml::name_is(n, "cr")) && in_r) {
          if (xml::attr(tag.attrs, "type") != "page") raw("<br/>");
        } else if (xml::name_is(n, "fldchar")) {
          // Between "begin" and "separate" is the field's code, not its text.
          const std::string_view v = xml::attr(tag.attrs, "fldCharType");
          field_code = v == "begin" ? true : v == "separate" || v == "end" ? false : field_code;
        } else if (xml::name_is(n, "blip") || xml::name_is(n, "imagedata")) {
          std::string_view rid = xml::attr(tag.attrs, "embed");
          if (rid.empty()) rid = xml::attr(tag.attrs, "id");
          if (!rid.empty()) picture(rid);
        }
        return true;
      });
  storage.close(doc);
  storage.remove(job.scratch_path);
  zip.close();
  if (!walked || !page.ok()) return Result::Unreadable;
  if (!any) return Result::NoText;
  page.end();
  return page.finish(true) ? Result::Ok : Result::Unreadable;
}

}  // namespace vellum::convert

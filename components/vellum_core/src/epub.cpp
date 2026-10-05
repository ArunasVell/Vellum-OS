#include "vellum/epub.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

#include "vellum/covers.h"
#include "vellum/text.h"
#include "vellum/xml.h"

namespace vellum {

struct Epub::ManifestItem {
  std::string_view id;
  std::string_view href;
  bool xhtml;
};

namespace {

constexpr int kMaxManifest = 4096;

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

// Whether a space-separated attribute value contains `word`.
bool has_word(std::string_view list, std::string_view word) {
  for (size_t a = 0; a < list.size();) {
    size_t b = a;
    while (b < list.size() && !is_space(list[b])) ++b;
    if (list.substr(a, b - a) == word) return true;
    a = b + 1;
  }
  return false;
}

// Copies element text into out: entities decoded, whitespace collapsed.
void copy_text(std::string_view s, char* out, size_t size) {
  size_t w = 0;
  bool space = false;
  for (size_t i = 0; i < s.size() && w + 5 < size;) {
    if (is_space(s[i])) {
      space = w > 0;
      ++i;
      continue;
    }
    if (space) out[w++] = ' ';
    space = false;
    size_t len = 0;
    if (s[i] == '&' && xml::decode_entity(s, i, out + w, len)) {
      w += len;
    } else {
      out[w++] = s[i++];
    }
  }
  out[w] = '\0';
}

int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Joins `base` (a directory, ending in '/' or empty) and a relative href into
// an archive path: percent-escapes decoded, "./" and "../" resolved, any
// "#fragment" dropped. Returns false when it does not fit.
bool resolve(std::string_view base, std::string_view href, char* out, size_t size) {
  if (const size_t hash = href.find('#'); hash != std::string_view::npos) href = href.substr(0, hash);
  char joined[512];
  size_t n = 0;
  for (std::string_view part : {base, href}) {
    for (size_t i = 0; i < part.size(); ++i) {
      if (n + 1 >= sizeof(joined)) return false;
      if (part[i] == '%' && i + 2 < part.size() && hex(part[i + 1]) >= 0 && hex(part[i + 2]) >= 0) {
        joined[n++] = static_cast<char>(hex(part[i + 1]) * 16 + hex(part[i + 2]));
        i += 2;
      } else {
        joined[n++] = part[i];
      }
    }
  }
  // Rebuild segment by segment.
  size_t w = 0;
  size_t i = 0;
  while (i < n) {
    size_t j = i;
    while (j < n && joined[j] != '/') ++j;
    const std::string_view seg(joined + i, j - i);
    if (seg == "..") {
      if (w > 0) --w;  // drop the trailing '/'
      while (w > 0 && out[w - 1] != '/') --w;
    } else if (!seg.empty() && seg != ".") {
      if (w + seg.size() + 2 >= size) return false;
      std::memcpy(out + w, seg.data(), seg.size());
      w += seg.size();
      if (j < n) out[w++] = '/';
    }
    i = j + 1;
  }
  out[w] = '\0';
  return true;
}

bool is_block(std::string_view n) {
  static constexpr const char* kBlocks[] = {
      "p",  "div",   "li", "blockquote", "section",    "article", "header",  "footer", "aside",
      "tr", "dd",    "dt", "pre",        "figcaption", "center",  "address", "nav",    "ul",
      "ol", "table", "td", "th",         "body",       "figure",  "dl"};
  for (const char* b : kBlocks) {
    if (xml::name_is(n, b)) return true;
  }
  return false;
}

bool is_heading(std::string_view n) {
  return n.size() == 2 && (n[0] == 'h' || n[0] == 'H') && n[1] >= '1' && n[1] <= '6';
}

bool is_skipped(std::string_view n) {
  return xml::name_is(n, "script") || xml::name_is(n, "style") || xml::name_is(n, "svg") || xml::name_is(n, "head") ||
         xml::name_is(n, "rt");
}

// Elements that never have content, even when HTML leaves out their "/>".
bool is_void(std::string_view n) {
  static constexpr const char* kVoid[] = {"br",   "img",  "hr",  "meta",   "link",  "input", "col",
                                          "area", "base", "wbr", "source", "param", "embed"};
  for (const char* v : kVoid) {
    if (xml::name_is(n, v)) return true;
  }
  return false;
}

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// anchor_hash of a name in lowercase (tag and class names in stylesheets).
uint32_t lower_hash(std::string_view n) {
  char low[48];
  const size_t len = n.size() < sizeof(low) ? n.size() : sizeof(low);
  for (size_t i = 0; i < len; ++i) low[i] = ascii_lower(n[i]);
  return anchor_hash(std::string_view(low, len));
}

// How the book styles an element: from its stylesheet rules, then its own
// style attribute. -1 where nothing says.
struct Look {
  int8_t align = -1;
  int8_t indent = -1;
  int8_t bold = -1;
  int8_t italic = -1;
  int8_t marker = -1;  // list bullets: 0 none
};

// Applies CSS declarations ("text-align: center; font-weight: bold") to `look`.
void apply_declarations(std::string_view decls, Look& look) {
  while (!decls.empty()) {
    const size_t semi = decls.find(';');
    const std::string_view d = decls.substr(0, semi);
    decls = semi == std::string_view::npos ? std::string_view{} : decls.substr(semi + 1);
    const size_t colon = d.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string_view prop = trim(d.substr(0, colon));
    std::string_view value = trim(d.substr(colon + 1));
    if (const size_t bang = value.find('!'); bang != std::string_view::npos) value = trim(value.substr(0, bang));
    if (xml::name_is(prop, "text-align")) {
      if (xml::name_is(value, "center")) look.align = 1;
      if (xml::name_is(value, "right") || xml::name_is(value, "end")) look.align = 2;
      if (xml::name_is(value, "left") || xml::name_is(value, "start") || xml::name_is(value, "justify")) {
        look.align = 0;
      }
    } else if (xml::name_is(prop, "text-indent")) {
      // Indented when the value is a positive length; 0 and hanging indents are flush.
      bool positive = false;
      for (char c : value) {
        if (c == '-') break;
        if (c >= '1' && c <= '9') positive = true;
        if (!((c >= '0' && c <= '9') || c == '.')) break;
      }
      look.indent = positive ? 1 : 0;
    } else if (xml::name_is(prop, "font-weight")) {
      const bool bold = xml::name_is(value, "bold") || xml::name_is(value, "bolder") ||
                        (value.size() == 3 && value[0] >= '6' && value[0] <= '9');
      look.bold = bold ? 1 : 0;
    } else if (xml::name_is(prop, "font-style")) {
      look.italic = xml::name_is(value, "italic") || xml::name_is(value, "oblique") ? 1 : 0;
    } else if (xml::name_is(prop, "list-style-type") || xml::name_is(prop, "list-style")) {
      look.marker = value.find("none") != std::string_view::npos ? 0 : 1;
    }
  }
}

void merge(Look& into, const Look& from) {
  if (from.align >= 0) into.align = from.align;
  if (from.indent >= 0) into.indent = from.indent;
  if (from.bold >= 0) into.bold = from.bold;
  if (from.italic >= 0) into.italic = from.italic;
  if (from.marker >= 0) into.marker = from.marker;
}

// The look the stylesheets and the style attribute give an element.
Look look_of(const xml::Tag& tag, const CssRule* css, uint32_t css_count) {
  Look look;
  if (css_count > 0) {
    uint32_t classes[4];
    int n = 0;
    const std::string_view list = xml::attr(tag.attrs, "class");
    for (size_t a = 0; a < list.size() && n < 4;) {
      size_t b = a;
      while (b < list.size() && !is_space(list[b])) ++b;
      if (b > a) classes[n++] = lower_hash(list.substr(a, b - a));
      a = b + 1;
    }
    const uint32_t name = lower_hash(tag.name);
    for (uint32_t k = 0; k < css_count; ++k) {
      const CssRule& r = css[k];
      if (r.tag != 0 && r.tag != name) continue;
      bool match = r.cls == 0;
      for (int c = 0; c < n && !match; ++c) match = classes[c] == r.cls;
      if (match) merge(look, {r.align, r.indent, r.bold, r.italic, r.marker});
    }
  }
  if (const std::string_view style = xml::attr(tag.attrs, "style"); !style.empty()) apply_declarations(style, look);
  return look;
}

// Whether an element holds footnotes or endnotes, by its name or marks.
bool is_note(const xml::Tag& tag) {
  if (xml::name_is(tag.name, "aside")) return true;
  for (const char* word : {"footnote", "endnote", "rearnote", "note", "footnotes", "endnotes", "rearnotes"}) {
    if (has_word(xml::attr(tag.attrs, "type"), word)) return true;
  }
  for (const char* word : {"doc-footnote", "doc-endnote", "doc-endnotes"}) {
    if (has_word(xml::attr(tag.attrs, "role"), word)) return true;
  }
  for (const char* word : {"footnote", "endnote", "footnotes", "endnotes"}) {
    if (has_word(xml::attr(tag.attrs, "class"), word)) return true;
  }
  return false;
}

// Whether a link's words look like a footnote reference: "1", "12", "*",
// "[3]", "†", superscript digits.
bool looks_like_note(std::string_view t) {
  t = trim(t);
  if (t.empty()) return false;
  if (t.size() <= 2) return true;
  if (t.size() > 12) return false;
  for (size_t i = 0; i < t.size();) {
    const auto c = static_cast<uint8_t>(t[i]);
    if ((c >= '0' && c <= '9') || c == '[' || c == ']' || c == '(' || c == ')' || c == '*') {
      ++i;
    } else if (c == 0xC2 && i + 1 < t.size() && (t[i + 1] == '\xB9' || t[i + 1] == '\xB2' || t[i + 1] == '\xB3')) {
      i += 2;  // ¹ ² ³
    } else if (c == 0xE2 && i + 2 < t.size() &&
               ((t[i + 1] == '\x80' && (t[i + 2] == '\xA0' || t[i + 2] == '\xA1')) ||  // † ‡
                (t[i + 1] == '\x81' && static_cast<uint8_t>(t[i + 2]) >= 0xB0))) {     // ⁰ ⁴..⁹
      i += 3;
    } else {
      return false;
    }
  }
  return true;
}

// Links that leave the book: the reader cannot follow them.
bool is_external(std::string_view href) {
  return href.find("://") != std::string_view::npos || href.starts_with("mailto:") || href.starts_with("tel:") ||
         href.starts_with("javascript:");
}

// Gathers the text of an element whose start tag ended at pos, up to its
// closing tag, without the markup inside it.
void element_text(std::string_view s, size_t& pos, std::string_view name, char* out, size_t size) {
  char raw[512];
  size_t n = 0;
  size_t start;
  xml::Tag tag;
  for (;;) {
    const size_t from = pos;
    const bool more = xml::next_tag(s, pos, tag, start);
    const size_t to = more ? start : s.size();
    for (size_t i = from; i < to && n + 1 < sizeof(raw); ++i) raw[n++] = s[i];
    if (!more || (tag.closing && xml::name_is(tag.name, name))) break;
    if (xml::name_is(tag.name, "br") && n + 1 < sizeof(raw)) raw[n++] = ' ';
  }
  copy_text(trim(std::string_view(raw, n)), out, size);
}

}  // namespace

Epub::Epub()
    : buf_(new(std::nothrow) char[kBufferBytes + 1]),
      paragraphs_(new(std::nothrow) const char*[kMaxParagraphs]),
      manifest_(new(std::nothrow) ManifestItem[kMaxManifest]),
      toc_(new(std::nothrow) TocEntry[kMaxToc]),
      toc_anchor_(new(std::nothrow) char[kMaxToc][48]),
      runs_(new(std::nothrow) StyleRun[kMaxRuns]),
      links_(new(std::nothrow) Link[kMaxLinks]),
      pictures_(new(std::nothrow) Picture[kMaxPictures]),
      anchors_(new(std::nothrow) Anchor[kMaxAnchors]),
      css_(new(std::nothrow) CssRule[kMaxCss]) {}

Epub::~Epub() {
  close();
  delete[] buf_;
  delete[] paragraphs_;
  delete[] manifest_;
  delete[] toc_;
  delete[] toc_anchor_;
  delete[] runs_;
  delete[] links_;
  delete[] pictures_;
  delete[] anchors_;
  delete[] css_;
}

void Epub::close() {
  if (file_ != nullptr) storage_->close(file_);
  file_ = nullptr;
  zip_.close();
  spine_count_ = 0;
  loaded_ = -1;
  paragraph_count_ = 0;
  toc_count_ = 0;
  cover_ = -1;
  info_ = {};
  chapter_bytes_ = 0;
  run_count_ = link_count_ = picture_count_ = anchor_count_ = css_count_ = 0;
}

int32_t Epub::read_entry(int i) {
  const int32_t n = zip_.extract(i, reinterpret_cast<uint8_t*>(buf_), kBufferBytes);
  if (n >= 0) buf_[n] = '\0';
  loaded_ = -1;  // the buffer no longer holds a chapter
  return n;
}

bool Epub::open(Storage& storage, const char* path) {
  close();
  if (buf_ == nullptr || paragraphs_ == nullptr || manifest_ == nullptr || toc_ == nullptr || toc_anchor_ == nullptr) {
    return false;
  }
  storage_ = &storage;
  file_ = storage.open(path);
  if (file_ == nullptr) return false;
  if (!zip_.open(file_)) {
    close();
    return false;
  }
  // FNV-1a over the path and size, as for covers.
  key_ = 2166136261u;
  for (const char* p = path; *p; ++p) key_ = (key_ ^ static_cast<uint8_t>(*p)) * 16777619u;
  for (int i = 0; i < 4; ++i) key_ = (key_ ^ ((file_->size() >> (8 * i)) & 0xFF)) * 16777619u;

  // META-INF/container.xml names the package file.
  const int container = zip_.find("META-INF/container.xml");
  const int32_t n = container >= 0 ? read_entry(container) : -1;
  char opf[256] = {};
  if (n > 0) {
    const std::string_view s(buf_, static_cast<size_t>(n));
    size_t pos = 0;
    size_t start;
    xml::Tag tag;
    while (xml::next_tag(s, pos, tag, start)) {
      if (!tag.closing && xml::name_is(tag.name, "rootfile")) {
        const std::string_view p = xml::attr(tag.attrs, "full-path");
        resolve({}, p, opf, sizeof(opf));
        break;
      }
    }
  }
  if (opf[0] == '\0' || !read_package(opf)) {
    close();
    return false;
  }
  if (info_.title[0] == '\0') {
    // Fall back to the file name without its folder and extension.
    const char* name = std::strrchr(path, '/');
    std::snprintf(info_.title, sizeof(info_.title), "%s", name ? name + 1 : path);
    if (char* dot = std::strrchr(info_.title, '.')) *dot = '\0';
  }
  return true;
}

bool Epub::read_package(const char* opf_path) {
  const int entry = zip_.find(opf_path);
  const int32_t n = entry >= 0 ? read_entry(entry) : -1;
  if (n <= 0) return false;
  const std::string_view s(buf_, static_cast<size_t>(n));
  const char* slash = std::strrchr(opf_path, '/');
  const std::string_view base(opf_path, slash ? static_cast<size_t>(slash - opf_path + 1) : 0);

  int items = 0;
  size_t pos = 0;
  size_t start;
  xml::Tag tag;
  bool in_spine = false;
  // The contents files are read after the package: their names would not
  // survive reading another entry into buf_.
  char nav[512] = {};
  char ncx[512] = {};
  // The cover: EPUB 3 marks its manifest item; EPUB 2 names the item's id
  // in <meta name="cover" content="...">, which may come before the item.
  char cover[512] = {};
  std::string_view cover_id;
  while (xml::next_tag(s, pos, tag, start)) {
    if (tag.closing) {
      if (xml::name_is(tag.name, "spine")) in_spine = false;
      continue;
    }
    // Text elements: up to the matching close tag.
    char* field = nullptr;
    size_t field_size = 0;
    if (xml::name_is(tag.name, "title") && info_.title[0] == '\0') {
      field = info_.title;
      field_size = sizeof(info_.title);
    } else if (xml::name_is(tag.name, "creator") && info_.author[0] == '\0') {
      field = info_.author;
      field_size = sizeof(info_.author);
    } else if (xml::name_is(tag.name, "language") && info_.language[0] == '\0') {
      field = info_.language;
      field_size = sizeof(info_.language);
    }
    if (field != nullptr && !tag.self_closing) {
      const size_t end = s.find('<', pos);
      if (end != std::string_view::npos) copy_text(trim(s.substr(pos, end - pos)), field, field_size);
      continue;
    }
    if (xml::name_is(tag.name, "item") && items < kMaxManifest) {
      const std::string_view type = xml::attr(tag.attrs, "media-type");
      manifest_[items++] = {xml::attr(tag.attrs, "id"), xml::attr(tag.attrs, "href"),
                            type == "application/xhtml+xml" || type == "text/html"};
      const bool image = type == "image/jpeg" || type == "image/jpg" || type == "image/png";
      if (image && has_word(xml::attr(tag.attrs, "properties"), "cover-image")) {
        resolve(base, manifest_[items - 1].href, cover, sizeof(cover));
      }
      if (has_word(xml::attr(tag.attrs, "properties"), "nav") && nav[0] == '\0')
        resolve(base, manifest_[items - 1].href, nav, sizeof(nav));
      if (type == "application/x-dtbncx+xml" && ncx[0] == '\0') {
        resolve(base, manifest_[items - 1].href, ncx, sizeof(ncx));
      }
    } else if (xml::name_is(tag.name, "meta") && xml::attr(tag.attrs, "name") == "cover") {
      cover_id = xml::attr(tag.attrs, "content");
    } else if (xml::name_is(tag.name, "spine")) {
      in_spine = !tag.self_closing;
    } else if (in_spine && xml::name_is(tag.name, "itemref") && spine_count_ < kMaxChapters) {
      const std::string_view idref = xml::attr(tag.attrs, "idref");
      for (int k = 0; k < items; ++k) {
        if (manifest_[k].id != idref || !manifest_[k].xhtml) continue;
        char path[512];
        if (!resolve(base, manifest_[k].href, path, sizeof(path))) break;
        const int z = zip_.find(path);
        if (z >= 0) spine_[spine_count_++] = static_cast<uint16_t>(z);
        break;
      }
    }
  }
  if (spine_count_ == 0) return false;
  if (cover[0] == '\0' && !cover_id.empty()) {
    for (int k = 0; k < items; ++k) {
      if (manifest_[k].id == cover_id) resolve(base, manifest_[k].href, cover, sizeof(cover));
    }
  }
  if (cover[0] != '\0') cover_ = zip_.find(cover);
  if (nav[0] != '\0') read_nav(nav);
  if (toc_count_ == 0 && ncx[0] != '\0') read_ncx(ncx);
  return true;
}

void Epub::add_toc(std::string_view base, std::string_view href, const char* title, int depth) {
  if (toc_count_ >= kMaxToc || title[0] == '\0') return;
  char path[512];
  if (!resolve(base, href, path, sizeof(path))) return;
  const int z = zip_.find(path);
  if (z < 0) return;
  for (int k = 0; k < spine_count_; ++k) {
    if (spine_[k] != z) continue;
    TocEntry& e = toc_[toc_count_];
    e = {};
    e.chapter = static_cast<uint16_t>(k);
    e.depth = static_cast<uint8_t>(depth < 0 ? 0 : depth > 255 ? 255 : depth);
    std::snprintf(e.title, sizeof(e.title), "%s", title);
    char* anchor = toc_anchor_[toc_count_];
    anchor[0] = '\0';
    const size_t hash = href.find('#');
    const std::string_view frag = hash == std::string_view::npos ? std::string_view{} : href.substr(hash + 1);
    if (!frag.empty() && frag.size() < sizeof(toc_anchor_[0])) {
      std::memcpy(anchor, frag.data(), frag.size());
      anchor[frag.size()] = '\0';
    }
    if (anchor[0] == '\0') e.paragraph = 0;  // the chapter's start
    ++toc_count_;
    return;
  }
}

void Epub::read_nav(const char* path) {
  const int entry = zip_.find(path);
  const int32_t n = entry >= 0 ? read_entry(entry) : -1;
  if (n <= 0) return;
  const std::string_view s(buf_, static_cast<size_t>(n));
  const char* slash = std::strrchr(path, '/');
  const std::string_view base(path, slash ? static_cast<size_t>(slash - path + 1) : 0);

  // Use the nav marked as the table of contents, or the first one.
  size_t from = std::string_view::npos;
  size_t pos = 0;
  size_t start;
  xml::Tag tag;
  while (xml::next_tag(s, pos, tag, start)) {
    if (tag.closing || !xml::name_is(tag.name, "nav")) continue;
    if (from == std::string_view::npos) from = pos;
    if (has_word(xml::attr(tag.attrs, "type"), "toc")) {
      from = pos;
      break;
    }
  }
  if (from == std::string_view::npos) return;

  pos = from;
  int lists = 0;  // open <ol> elements
  while (xml::next_tag(s, pos, tag, start)) {
    if (xml::name_is(tag.name, "nav") && tag.closing) break;
    if (xml::name_is(tag.name, "ol")) {
      if (!tag.self_closing) lists += tag.closing ? -1 : 1;
    } else if (xml::name_is(tag.name, "a") && !tag.closing && !tag.self_closing) {
      const std::string_view href = xml::attr(tag.attrs, "href");
      char title[96];
      element_text(s, pos, "a", title, sizeof(title));
      if (!href.empty()) add_toc(base, href, title, lists - 1);
    }
  }
}

void Epub::read_ncx(const char* path) {
  const int entry = zip_.find(path);
  const int32_t n = entry >= 0 ? read_entry(entry) : -1;
  if (n <= 0) return;
  const std::string_view s(buf_, static_cast<size_t>(n));
  const char* slash = std::strrchr(path, '/');
  const std::string_view base(path, slash ? static_cast<size_t>(slash - path + 1) : 0);

  size_t pos = 0;
  size_t start;
  xml::Tag tag;
  int depth = 0;  // open navPoint elements
  char title[96] = {};
  while (xml::next_tag(s, pos, tag, start)) {
    if (xml::name_is(tag.name, "navpoint")) {
      if (tag.self_closing) continue;
      depth += tag.closing ? -1 : 1;
      title[0] = '\0';
    } else if (depth > 0 && xml::name_is(tag.name, "text") && !tag.closing && !tag.self_closing) {
      element_text(s, pos, "text", title, sizeof(title));
    } else if (depth > 0 && xml::name_is(tag.name, "content") && !tag.closing) {
      add_toc(base, xml::attr(tag.attrs, "src"), title, depth - 1);
    }
  }
}

int32_t Epub::read_cover(const uint8_t** data) {
  if (cover_ < 0) return -1;
  const int32_t n = read_entry(cover_);
  if (n > 0) *data = reinterpret_cast<const uint8_t*>(buf_);
  return n;
}

uint32_t Epub::chapter_weight(int i) const { return (i >= 0 && i < spine_count_) ? zip_.size(spine_[i]) + 1 : 1; }

bool Epub::load_chapter(int i, Chapter& out) {
  if (i < 0 || i >= spine_count_) return false;
  if (loaded_ != i) {
    const int32_t n = read_entry(spine_[i]);
    if (n < 0) return false;
    chapter_bytes_ = static_cast<uint32_t>(n) + 1;
    read_css(spine_[i], static_cast<uint32_t>(n));
    // Contents entries that point inside this chapter learn their paragraph.
    struct Anchors {
      Epub* epub;
      int16_t entries[64];
      int count;
    } anchors{this, {}, 0};
    for (int k = 0; k < toc_count_ && anchors.count < 64; ++k) {
      if (toc_[k].chapter == i && toc_anchor_[k][0] != '\0') anchors.entries[anchors.count++] = static_cast<int16_t>(k);
    }
    AnchorHook hook;
    if (anchors.count > 0) {
      hook.ctx = &anchors;
      hook.found = [](void* ctx, std::string_view id, uint32_t paragraph) {
        auto* a = static_cast<Anchors*>(ctx);
        for (int k = 0; k < a->count; ++k) {
          const int e = a->entries[k];
          if (a->epub->toc_[e].paragraph == TocEntry::kUnknown && id == a->epub->toc_anchor_[e]) {
            a->epub->toc_[e].paragraph = paragraph;
          }
        }
      };
    }
    // Links and pictures name files relative to the chapter's own.
    struct Place {
      Epub* epub;
      std::string_view base;
    } place{this, zip_.name(spine_[i])};
    place.base = place.base.substr(0, place.base.rfind('/') + 1);
    Markup m;
    if (runs_ != nullptr && links_ != nullptr && pictures_ != nullptr && anchors_ != nullptr && css_ != nullptr) {
      m.runs = runs_;
      m.max_runs = kMaxRuns;
      m.links = links_;
      m.max_links = kMaxLinks;
      m.pictures = pictures_;
      m.max_pictures = kMaxPictures;
      m.anchors = anchors_;
      m.max_anchors = kMaxAnchors;
      m.css = css_;
      m.css_count = css_count_;
    }
    m.ctx = &place;
    m.entry_of = [](void* ctx, std::string_view src) {
      const auto* p = static_cast<const Place*>(ctx);
      char path[512];
      return resolve(p->base, src, path, sizeof(path)) ? p->epub->zip_.find(path) : -1;
    };
    m.chapter_of = [](void* ctx, std::string_view href) {
      const auto* p = static_cast<const Place*>(ctx);
      char path[512];
      const int z = resolve(p->base, href, path, sizeof(path)) ? p->epub->zip_.find(path) : -1;
      for (int k = 0; k < p->epub->spine_count_ && z >= 0; ++k) {
        if (p->epub->spine_[k] == z) return k;
      }
      return -1;
    };
    paragraph_count_ = to_paragraphs(buf_, static_cast<uint32_t>(n), paragraphs_, kMaxParagraphs, hook, &m);
    for (int k = 0; k < anchors.count; ++k) {
      TocEntry& e = toc_[anchors.entries[k]];
      if (e.paragraph == TocEntry::kUnknown) e.paragraph = 0;  // not found: the chapter's start
    }
    run_count_ = m.run_count;
    link_count_ = m.link_count;
    picture_count_ = m.picture_count;
    anchor_count_ = m.anchor_count;
    for (uint32_t k = 0; k < link_count_; ++k) {
      if (links_[k].chapter == Link::kThisChapter) links_[k].chapter = static_cast<uint16_t>(i);
    }
    loaded_ = i;
    measure_pictures();
  }
  out = {paragraphs_, paragraph_count_, "", ""};
  return true;
}

uint8_t* Epub::spare(uint32_t& size) const {
  const uint32_t at = (chapter_bytes_ + 3) & ~3u;
  size = at < kBufferBytes ? kBufferBytes - at : 0;
  return reinterpret_cast<uint8_t*>(buf_) + at;
}

void Epub::read_css(int entry, uint32_t len) {
  css_count_ = 0;
  if (css_ == nullptr) return;
  const std::string_view s(buf_, len);
  size_t end = s.find("<body");
  if (end == std::string_view::npos) end = s.find("<BODY");
  if (end == std::string_view::npos) return;  // no head
  const std::string_view head = s.substr(0, end);
  std::string_view base = zip_.name(entry);
  base = base.substr(0, base.rfind('/') + 1);
  size_t pos = 0;
  size_t start;
  xml::Tag tag;
  while (xml::next_tag(head, pos, tag, start)) {
    if (tag.closing) continue;
    if (xml::name_is(tag.name, "style") && !tag.self_closing) {
      const size_t close = head.find("</", pos);
      if (close == std::string_view::npos) break;
      css_count_ = parse_css(head.substr(pos, close - pos), css_, css_count_, kMaxCss);
      pos = close;
    } else if (xml::name_is(tag.name, "link") && has_word(xml::attr(tag.attrs, "rel"), "stylesheet")) {
      // Read past the chapter in buf_, which stays as it is.
      char path[512];
      if (!resolve(base, xml::attr(tag.attrs, "href"), path, sizeof(path))) continue;
      const int z = zip_.find(path);
      uint32_t room;
      uint8_t* at = spare(room);
      const int32_t n = z >= 0 ? zip_.extract(z, at, room) : -1;
      if (n > 0)
        css_count_ = parse_css({reinterpret_cast<char*>(at), static_cast<size_t>(n)}, css_, css_count_, kMaxCss);
    }
  }
}

void Epub::measure_pictures() {
  for (uint32_t k = 0; k < picture_count_; ++k) {
    Picture& p = pictures_[k];
    if (p.entry < 0) continue;
    // The size is in the first bytes: a PNG's first 24, a JPEG's frame
    // header after any embedded thumbnail, so read more until it is found.
    uint32_t room;
    uint8_t* at = spare(room);
    int w = 0, h = 0, found = -1;
    for (uint32_t want : {4096u, 65536u, room}) {
      want = std::min(want, room);
      const int32_t n = want > 0 ? zip_.extract_head(p.entry, at, want) : -1;
      if (n <= 0) break;
      found = covers::picture_size(at, static_cast<uint32_t>(n), w, h);
      if (found >= 0 || static_cast<uint32_t>(n) < want) break;  // known, or that was all of it
    }
    if (found == 1) {
      p.w = static_cast<uint16_t>(w);
      p.h = static_cast<uint16_t>(h);
    }
  }
}

const Anchor* Epub::find_anchor(uint32_t hash) const {
  for (uint32_t k = 0; k < anchor_count_; ++k) {
    if (anchors_[k].hash == hash) return &anchors_[k];
  }
  return nullptr;
}

ChapterMarkup Epub::markup() const {
  if (loaded_ < 0) return {};
  return {runs_, run_count_, links_, link_count_, pictures_, picture_count_};
}

bool Epub::follow(const Link& to, LinkTarget& out, char* note, size_t size) {
  const Link link = to;  // `to` may be in links_, which another chapter replaces
  const int here = loaded_;
  const int target = link.chapter == Link::kThisChapter ? here : link.chapter;
  if (here < 0 || target < 0 || target >= spine_count_) return false;
  Chapter ch;
  if (target != here && !load_chapter(target, ch)) {
    load_chapter(here, ch);
    return false;
  }
  out = {};
  out.chapter = static_cast<uint16_t>(target);
  const Anchor* a = link.anchor != 0 ? find_anchor(link.anchor) : nullptr;
  out.paragraph = a != nullptr ? a->paragraph : 0;
  out.note = a != nullptr && (link.note || a->note);
  if (out.note && note != nullptr && size > 4) {
    // The note's paragraphs, joined, without their marks or pictures.
    size_t n = 0;
    bool cut = false;
    for (uint32_t p = a->paragraph; p < a->end && p < paragraph_count_ && !cut; ++p) {
      const std::string_view text(paragraphs_[p]);
      const ParagraphMarks marks = read_marks(text);
      if (marks.picture || marks.rule) continue;
      const std::string_view words = text.substr(marks.length);
      if (words.empty()) continue;
      if (n > 0) note[n++] = '\n';
      size_t whole = n;  // the end of the last whole character
      for (char c : words) {
        if ((static_cast<uint8_t>(c) & 0xC0) != 0x80) whole = n;
        if (n + 4 >= size) {
          cut = true;
          n = whole;
          break;
        }
        note[n++] = c;
      }
    }
    if (cut) {
      std::memcpy(note + n, "\xE2\x80\xA6", 3);  // …
      n += 3;
    }
    note[n] = '\0';
    out.note = n > 0;
  }
  if (target != here) load_chapter(here, ch);
  return true;
}

bool Epub::draw_picture(Framebuffer& fb, const Picture& p, Rect r) {
  if (p.entry < 0 || loaded_ < 0 || storage_ == nullptr) return false;
  char path[96];
  covers::picture_path(key_, p.entry, r.w, r.h, path, sizeof(path));
  // Kept on the card from an earlier visit, or decoded from the book now:
  // either way read into buf_ past the chapter.
  uint32_t room;
  uint8_t* at = spare(room);
  if (covers::draw_kept(*storage_, path, fb, r, at, room)) return true;
  const int32_t n = zip_.extract(p.entry, at, room);
  return n > 0 && covers::draw_new(storage_, path, fb, r, at, static_cast<uint32_t>(n), at, room);
}

namespace {

// Index of the brace closing the block that opens at s[open], or npos.
size_t block_end(std::string_view s, size_t open) {
  int depth = 0;
  for (size_t i = open; i < s.size(); ++i) {
    if (s[i] == '{') ++depth;
    if (s[i] == '}' && --depth == 0) return i;
  }
  return std::string_view::npos;
}

// A plain selector: "p", ".center" or "p.center"; sets the hashes.
bool simple_selector(std::string_view sel, uint32_t& tag, uint32_t& cls) {
  sel = trim(sel);
  if (sel.empty()) return false;
  for (char c : sel) {
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    if (!ok) return false;
  }
  const size_t dot = sel.find('.');
  const std::string_view t = sel.substr(0, dot);
  const std::string_view c = dot == std::string_view::npos ? std::string_view{} : sel.substr(dot + 1);
  if (c.find('.') != std::string_view::npos || (dot != std::string_view::npos && c.empty())) return false;
  tag = t.empty() ? 0 : lower_hash(t);
  cls = c.empty() ? 0 : lower_hash(c);
  return true;
}

// An element being read, and what it passes on to the text inside it.
struct Open {
  uint32_t name = 0;  // lower_hash of its name
  uint8_t flags = 0;  // StyleRun flags of its text
  int8_t align = -1;  // as Look
  int8_t indent = -1;
  uint8_t level = 0;    // lists and quotations it is in
  bool row = false;     // in a table row
  bool notes = false;   // in footnotes or endnotes
  bool sup = false;     // in <sup>
  bool list = false;    // it is a <ul> or <ol>
  bool plain = false;   // its list items have no bullets
  int16_t anchor = -1;  // the anchor it starts
  int16_t link = -1;    // the link it is
};

constexpr int kMaxDepth = 48;
constexpr int kMaxLists = 8;
constexpr int kMaxLevelMarks = 4;
constexpr size_t kMaxAlt = 120;

}  // namespace

uint32_t Epub::parse_css(std::string_view css, CssRule* out, uint32_t count, uint32_t max) {
  size_t i = 0;
  while (i < css.size() && count < max) {
    const size_t open = css.find('{', i);
    if (open == std::string_view::npos) break;
    const size_t close = block_end(css, open);
    if (close == std::string_view::npos) break;
    std::string_view head = css.substr(i, open - i);
    if (const size_t c = head.rfind("*/"); c != std::string_view::npos) head = head.substr(c + 2);  // a comment
    head = trim(head);
    const std::string_view body = css.substr(open + 1, close - open - 1);
    i = close + 1;
    if (head.starts_with("@")) {
      if (head.starts_with("@media")) count = parse_css(body, out, count, max);
      continue;
    }
    Look look;
    apply_declarations(body, look);
    if (look.align < 0 && look.indent < 0 && look.bold < 0 && look.italic < 0 && look.marker < 0) continue;
    while (!head.empty() && count < max) {
      const size_t comma = head.find(',');
      uint32_t tag, cls;
      if (simple_selector(head.substr(0, comma), tag, cls)) {
        out[count++] = {tag, cls, look.align, look.indent, look.bold, look.italic, look.marker};
      }
      head = comma == std::string_view::npos ? std::string_view{} : head.substr(comma + 1);
    }
  }
  return count;
}

uint32_t Epub::to_paragraphs(char* s, uint32_t len, const char** paragraphs, uint32_t max, AnchorHook anchors,
                             Markup* markup) {
  const std::string_view v(s, len);
  uint32_t count = 0;
  size_t w = 0;          // write position; never passes the read position
  size_t para = 0;       // where the current paragraph starts
  bool space = false;    // whitespace seen since the last character
  bool heading = false;  // inside h1..h6
  int skip = 0;          // depth inside script, style, svg...
  int svg = 0;           // depth inside svg (counted in skip too)
  bool in_body = v.find("<body") == std::string_view::npos && v.find("<BODY") == std::string_view::npos;

  Markup none;
  Markup& m = markup != nullptr ? *markup : none;
  m.run_count = m.link_count = m.picture_count = m.anchor_count = 0;

  // The open elements, innermost last; what they pass on is copied down.
  Open stack[kMaxDepth];
  int depth = 0;
  const Open outside;
  struct List {
    bool ordered;
    int next;
  } lists[kMaxLists];
  int list_count = 0;
  char item[16] = {};  // the bullet or number of the list item being started
  size_t item_len = 0;
  bool item_pending = false;  // its first paragraph has not started yet
  bool cell_gap = false;      // a new table cell: " | " before the next character
  bool flush = false;         // after a line break: the line is not indented
  uint8_t run_flags = 0;      // emphasis of the last run written

  auto top = [&]() -> const Open& { return depth > 0 ? stack[depth - 1] : outside; };
  auto here = [&] { return TextPos{count, static_cast<uint32_t>(w - para)}; };

  // Emphasis changes from here on.
  auto set_flags = [&](uint8_t f) {
    if (f == run_flags || m.max_runs == 0) return;
    uint32_t& n = m.run_count;
    const TextPos at = here();
    if (n > 0 && m.runs[n - 1].at == at) {
      m.runs[n - 1].flags = f;
      if (n >= 2 && m.runs[n - 2].flags == f) --n;  // back to what it was
    } else if (n + 1 < m.max_runs) {
      m.runs[n++] = {at, f};
    } else if (n + 1 == m.max_runs) {
      m.runs[n++] = {at, 0};  // full: plain from here on
      f = 0;
    } else {
      return;
    }
    run_flags = f;
  };

  auto finish = [&] {
    if (w > para && count < max) {
      s[w++] = '\0';
      paragraphs[count++] = s + para;
    } else {
      w = para;
    }
    para = w;
    space = false;
    cell_gap = false;
    flush = false;
  };

  // The marks that start a paragraph, written before its first character
  // when there is room (`room` bytes before it are free; always, in practice).
  auto begin_paragraph = [&](size_t room) {
    const Open& o = top();
    char marks[16];
    size_t n = 0;
    if (heading) marks[n++] = kHeadingMark;
    for (int k = 0; k < o.level && k < kMaxLevelMarks; ++k) marks[n++] = kLevelMark;
    if (o.row) marks[n++] = kRowMark;
    const bool bullet = item_pending && item_len > 0;
    if (bullet) marks[n++] = kItemMark;
    if (!heading) {
      if (o.align == 1) marks[n++] = kCenterMark;
      if (o.align == 2) marks[n++] = kRightMark;
      if (o.indent == 0 || flush) {
        marks[n++] = kNoIndentMark;
      } else if (o.indent == 1) {
        marks[n++] = kIndentMark;
      }
    }
    const size_t need = n + (bullet ? item_len + 1 : 0);
    if (need <= room) {
      std::memcpy(s + w, marks, n);
      w += n;
      if (bullet) {
        std::memcpy(s + w, item, item_len);
        w += item_len;
        s[w++] = ' ';
      }
    } else if (heading && room > 0) {
      s[w++] = kHeadingMark;
    }
    item_pending = false;
    flush = false;
  };

  // A picture: a paragraph of its own, holding its alt text.
  auto add_picture = [&](std::string_view src, std::string_view alt) {
    if (m.picture_count >= m.max_pictures) return;
    const int entry = m.entry_of != nullptr ? m.entry_of(m.ctx, src) : -1;
    finish();
    if (count >= max) return;
    s[w++] = kPictureMark;
    alt = trim(alt);
    size_t n = std::min(alt.size(), kMaxAlt);
    while (n > 0 && n < alt.size() && (static_cast<uint8_t>(alt[n]) & 0xC0) == 0x80) --n;        // a whole character
    for (size_t k = 0; k < n; ++k) s[w++] = is_mark(alt[k]) || is_space(alt[k]) ? ' ' : alt[k];  // copies forward
    m.pictures[m.picture_count++] = {count, entry, 0, 0};
    finish();
  };

  // Leaves elements down to depth d, ending their anchors and links.
  auto pop_to = [&](int d) {
    while (depth > d) {
      const Open& o = stack[--depth];
      if (o.list && list_count > 0) --list_count;
      if (o.anchor >= 0) {
        Anchor& a = m.anchors[o.anchor];
        a.end = std::max(a.paragraph + 1, count + (w > para ? 1 : 0));
      }
      if (o.link >= 0) {
        Link& l = m.links[o.link];
        l.end = here();
        if (!l.note && l.anchor != 0) {
          const bool same = l.begin.paragraph == count && l.begin.offset <= w - para;
          l.note = o.sup ||
                   (same && looks_like_note(std::string_view(s + para + l.begin.offset, w - para - l.begin.offset)));
        }
      }
    }
    set_flags(top().flags);
  };

  size_t pos = 0;
  for (;;) {
    size_t start;
    xml::Tag tag;
    const size_t text_begin = pos;
    const bool more = xml::next_tag(v, pos, tag, start);
    const size_t text_end = more ? start : v.size();
    // Text between the previous tag and this one.
    if (in_body && skip == 0) {
      for (size_t r = text_begin; r < text_end;) {
        const char c = s[r];
        if (c == '<') {
          // A comment, processing instruction or CDATA block that next_tag
          // stepped over: skip it too.
          const bool comment = v.compare(r, 4, "<!--") == 0;
          const bool cdata = v.compare(r, 9, "<![CDATA[") == 0;
          const size_t close = v.find(comment ? "-->" : cdata ? "]]>" : ">", r);
          if (close == std::string_view::npos || close >= text_end) break;
          r = close + (comment || cdata ? 3 : 1);
          continue;
        }
        if (is_space(c)) {
          space = w > para;
          ++r;
          continue;
        }
        const size_t at = r;  // where the character starts: the bytes before it are free
        char ch[4];
        size_t n = 1;
        if (c == '&') {
          size_t p = r;
          if (xml::decode_entity(v, p, ch, n)) {
            r = p;
          } else {
            ch[0] = c;
            n = 1;
            ++r;
          }
          if (n == 1 && is_space(ch[0])) {
            space = w > para;
            continue;
          }
          if (n == 0) continue;  // soft hyphen
        } else if (c == '\xC2' && r + 1 < text_end && s[r + 1] == '\xAD') {
          r += 2;  // soft hyphen
          continue;
        } else {
          ch[0] = c;
          ++r;
        }
        if (w == para) {
          begin_paragraph(at - w);
        } else if (cell_gap && at - w >= 3) {
          s[w++] = ' ';
          s[w++] = '|';
          s[w++] = ' ';
        } else if (space || cell_gap) {
          s[w++] = ' ';
        }
        space = false;
        cell_gap = false;
        for (size_t k = 0; k < n; ++k) s[w++] = ch[k];
      }
    }
    if (!more) break;
    const std::string_view name = tag.name;
    if (xml::name_is(name, "body")) {
      if (tag.closing) break;
      in_body = true;
      continue;
    }
    if (is_skipped(name)) {
      if (!tag.self_closing) {
        skip += tag.closing ? (skip > 0 ? -1 : 0) : 1;
        if (xml::name_is(name, "svg")) svg += tag.closing ? (svg > 0 ? -1 : 0) : 1;
      }
      continue;
    }
    if (skip > 0) {
      // A picture wrapped in <svg>, as full-page pictures often are.
      if (svg > 0 && in_body && !tag.closing && xml::name_is(name, "image")) {
        add_picture(xml::attr(tag.attrs, "href"), {});
      }
      continue;
    }
    if (!in_body) continue;

    const uint32_t hash = lower_hash(name);
    const bool row = top().row;
    if (tag.closing) {
      if (is_heading(name)) {
        finish();
        heading = false;
      } else if (xml::name_is(name, "tr") || (is_block(name) && !row)) {
        finish();
      }
      for (int d = depth - 1; d >= 0; --d) {
        if (stack[d].name == hash) {
          pop_to(d);
          break;
        }
      }
      continue;
    }

    const Look look = look_of(tag, m.css, m.css_count);
    // HTML may leave out </p> and </li>: a new one ends the last.
    if (depth > 0 && stack[depth - 1].name == hash && (xml::name_is(name, "p") || xml::name_is(name, "li"))) {
      finish();
      pop_to(depth - 1);
    }
    if (is_heading(name)) {
      finish();
      heading = true;
    } else if (xml::name_is(name, "br")) {
      if (row) {
        space = w > para;
      } else {
        finish();
        flush = true;  // the next line goes on, not indented
      }
    } else if (xml::name_is(name, "hr")) {
      finish();
      if (count < max) {
        s[w++] = kRuleMark;  // "<hr>" has room for it
        finish();
      }
    } else if (xml::name_is(name, "img")) {
      add_picture(xml::attr(tag.attrs, "src"), xml::attr(tag.attrs, "alt"));
    } else if (xml::name_is(name, "tr")) {
      finish();
    } else if (xml::name_is(name, "td") || xml::name_is(name, "th")) {
      if (row) {
        cell_gap = w > para;
      } else {
        finish();
      }
    } else if (is_block(name)) {
      if (row) {
        space = w > para;
      } else {
        finish();
      }
    }
    if (xml::name_is(name, "li")) {
      // The bullet or number its first paragraph starts with.
      item_pending = true;
      item_len = 0;
      if (!top().plain && look.marker != 0) {
        if (list_count > 0 && lists[list_count - 1].ordered) {
          item_len = static_cast<size_t>(std::snprintf(item, sizeof(item), "%d.", lists[list_count - 1].next++));
        } else {
          const char* bullet = list_count % 2 == 0 ? "\xE2\x80\x93" : "\xE2\x80\xA2";  // – or •
          item_len = std::strlen(bullet);
          std::memcpy(item, bullet, item_len);
        }
      }
    }

    // An id (or an old-style <a name>) marks where a link can land: the
    // paragraph being collected. Its attributes are still intact here, as
    // writing never passes the start of this tag.
    std::string_view id = xml::attr(tag.attrs, "id");
    if (id.empty() && xml::name_is(name, "a")) id = xml::attr(tag.attrs, "name");
    if (!id.empty() && anchors.found != nullptr) anchors.found(anchors.ctx, id, count);
    int anchor = -1;
    if (!id.empty() && m.anchor_count < m.max_anchors) {
      anchor = static_cast<int>(m.anchor_count);
      m.anchors[m.anchor_count++] = {anchor_hash(id), count, count + 1, top().notes || is_note(tag)};
    }
    if (tag.self_closing || is_void(name) || depth >= kMaxDepth) continue;

    // The element passes its look on to the text inside it.
    Open& o = stack[depth];
    o = top();
    ++depth;
    o.name = hash;
    o.list = false;
    o.anchor = static_cast<int16_t>(anchor);
    o.link = -1;
    if (xml::name_is(name, "b") || xml::name_is(name, "strong") || xml::name_is(name, "th")) {
      o.flags |= StyleRun::kBold;
    }
    if (xml::name_is(name, "i") || xml::name_is(name, "em") || xml::name_is(name, "cite") ||
        xml::name_is(name, "dfn") || xml::name_is(name, "var")) {
      o.flags |= StyleRun::kItalic;
    }
    if (look.bold >= 0) o.flags = look.bold ? (o.flags | StyleRun::kBold) : (o.flags & ~StyleRun::kBold);
    if (look.italic >= 0) o.flags = look.italic ? (o.flags | StyleRun::kItalic) : (o.flags & ~StyleRun::kItalic);
    if (xml::name_is(name, "center")) o.align = 1;
    if (const std::string_view a = xml::attr(tag.attrs, "align"); !a.empty()) {
      o.align = xml::name_is(a, "center") ? 1 : xml::name_is(a, "right") ? 2 : 0;
    }
    if (look.align >= 0) o.align = look.align;
    if (look.indent >= 0) o.indent = look.indent;
    if (xml::name_is(name, "ul") || xml::name_is(name, "ol")) {
      ++o.level;
      o.plain = look.marker == 0 || xml::name_is(xml::attr(tag.attrs, "type"), "none");
      if (list_count < kMaxLists) {
        int first = 0;
        for (char c : xml::attr(tag.attrs, "start")) {
          if (c < '0' || c > '9' || first > 99999) break;
          first = first * 10 + (c - '0');
        }
        lists[list_count++] = {xml::name_is(name, "ol"), first > 0 ? first : 1};
        o.list = true;
      }
    } else if (xml::name_is(name, "blockquote") || xml::name_is(name, "dd")) {
      ++o.level;
    }
    if (xml::name_is(name, "tr")) o.row = true;
    if (xml::name_is(name, "sup")) o.sup = true;
    if (anchor >= 0 && m.anchors[anchor].note) o.notes = true;
    // A link within the book: underlined, and known by where it leads.
    if (xml::name_is(name, "a") && m.link_count < m.max_links) {
      const std::string_view href = xml::attr(tag.attrs, "href");
      if (!href.empty() && !is_external(href)) {
        const size_t hash_at = href.find('#');
        const std::string_view path = href.substr(0, hash_at);
        const std::string_view frag = hash_at == std::string_view::npos ? std::string_view{} : href.substr(hash_at + 1);
        int chapter = Link::kThisChapter;
        if (!path.empty()) chapter = m.chapter_of != nullptr ? m.chapter_of(m.ctx, path) : -1;
        if (chapter >= 0) {
          Link& l = m.links[m.link_count];
          l = {};
          l.begin = l.end = here();
          l.anchor = frag.empty() ? 0 : anchor_hash(frag);
          l.chapter = static_cast<uint16_t>(chapter);
          l.note = has_word(xml::attr(tag.attrs, "type"), "noteref") ||
                   has_word(xml::attr(tag.attrs, "role"), "doc-noteref");
          o.link = static_cast<int16_t>(m.link_count++);
          o.flags |= StyleRun::kLink;
        }
      }
    }
    set_flags(o.flags);
  }
  finish();
  return count;
}

}  // namespace vellum

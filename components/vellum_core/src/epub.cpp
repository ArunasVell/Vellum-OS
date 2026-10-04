#include "vellum/epub.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

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

}  // namespace

Epub::Epub()
    : buf_(new(std::nothrow) char[kBufferBytes + 1]),
      paragraphs_(new(std::nothrow) const char*[kMaxParagraphs]),
      manifest_(new(std::nothrow) ManifestItem[kMaxManifest]) {}

Epub::~Epub() {
  close();
  delete[] buf_;
  delete[] paragraphs_;
  delete[] manifest_;
}

void Epub::close() {
  if (file_ != nullptr) storage_->close(file_);
  file_ = nullptr;
  zip_.close();
  spine_count_ = 0;
  loaded_ = -1;
  paragraph_count_ = 0;
  info_ = {};
}

int32_t Epub::read_entry(int i) {
  const int32_t n = zip_.extract(i, reinterpret_cast<uint8_t*>(buf_), kBufferBytes);
  if (n >= 0) buf_[n] = '\0';
  loaded_ = -1;  // the buffer no longer holds a chapter
  return n;
}

bool Epub::open(Storage& storage, const char* path) {
  close();
  if (buf_ == nullptr || paragraphs_ == nullptr || manifest_ == nullptr) return false;
  storage_ = &storage;
  file_ = storage.open(path);
  if (file_ == nullptr) return false;
  if (!zip_.open(file_)) {
    close();
    return false;
  }

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
  return spine_count_ > 0;
}

uint32_t Epub::chapter_weight(int i) const { return (i >= 0 && i < spine_count_) ? zip_.size(spine_[i]) + 1 : 1; }

bool Epub::load_chapter(int i, Chapter& out) {
  if (i < 0 || i >= spine_count_) return false;
  if (loaded_ != i) {
    const int32_t n = read_entry(spine_[i]);
    if (n < 0) return false;
    paragraph_count_ = to_paragraphs(buf_, static_cast<uint32_t>(n), paragraphs_, kMaxParagraphs);
    loaded_ = i;
  }
  out = {paragraphs_, paragraph_count_, "", ""};
  return true;
}

uint32_t Epub::to_paragraphs(char* s, uint32_t len, const char** paragraphs, uint32_t max) {
  const std::string_view v(s, len);
  uint32_t count = 0;
  size_t w = 0;          // write position; never passes the read position
  size_t para = 0;       // where the current paragraph starts
  bool space = false;    // whitespace seen since the last character
  bool heading = false;  // inside h1..h6
  int skip = 0;          // depth inside script, style, svg...
  bool in_body = v.find("<body") == std::string_view::npos && v.find("<BODY") == std::string_view::npos;

  auto finish = [&] {
    if (w > para && count < max) {
      s[w++] = '\0';
      paragraphs[count++] = s + para;
    } else {
      w = para;
    }
    para = w;
    space = false;
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
        if (space) s[w++] = ' ';
        space = false;
        if (w == para && heading) s[w++] = kHeadingMark;
        for (size_t k = 0; k < n; ++k) s[w++] = ch[k];
      }
    }
    if (!more) break;
    const std::string_view name = tag.name;
    if (xml::name_is(name, "body")) {
      if (tag.closing) break;
      in_body = true;
    } else if (is_skipped(name)) {
      if (!tag.self_closing) skip += tag.closing ? (skip > 0 ? -1 : 0) : 1;
    } else if (skip > 0) {
      // ignore tags inside skipped content
    } else if (is_heading(name)) {
      finish();
      heading = !tag.closing;
    } else if (xml::name_is(name, "br")) {
      finish();
    } else if (xml::name_is(name, "hr")) {
      finish();
      if (count < max && in_body) paragraphs[count++] = "";  // scene break: a blank line
    } else if (is_block(name)) {
      finish();
    }
  }
  finish();
  return count;
}

}  // namespace vellum

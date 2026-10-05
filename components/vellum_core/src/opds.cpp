#include "vellum/opds.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/textfile.h"
#include "vellum/xml.h"

namespace vellum {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// Bytes in the UTF-8 sequence that starts with `lead`.
size_t utf8_length(unsigned char lead) {
  if (lead >= 0xF0) return 4;
  if (lead >= 0xE0) return 3;
  if (lead >= 0xC0) return 2;
  return 1;
}

// Builds a field's text: runs of white space become one space, markup is
// left out when asked, and the end never cuts a character in half.
class Text {
 public:
  Text(char* out, size_t size, bool strip) : out_(out), size_(size), strip_(strip) {
    if (size_ > 0) out_[0] = '\0';
  }

  void add(const char* seq, size_t n) {
    const char c = seq[0];
    if (strip_ && n == 1) {
      if (c == '<') {
        markup_ = true;
        return;
      }
      if (markup_) {
        if (c == '>') {
          markup_ = false;
          space_ = len_ > 0;
        }
        return;
      }
    } else if (markup_) {
      return;
    }
    if (n == 1 && is_space(c)) {
      space_ = len_ > 0;
      return;
    }
    if (full_ || len_ + n + (space_ ? 1 : 0) + 1 > size_) {
      full_ = true;
      return;
    }
    if (space_) out_[len_++] = ' ';
    space_ = false;
    std::memcpy(out_ + len_, seq, n);
    len_ += n;
    out_[len_] = '\0';
  }

 private:
  char* out_;
  size_t size_;
  size_t len_ = 0;
  bool strip_;
  bool markup_ = false;
  bool space_ = false;
  bool full_ = false;
};

// Decodes character references in s into `out`.
void decode(std::string_view s, Text& out) {
  size_t pos = 0;
  while (pos < s.size()) {
    if (s[pos] == '&') {
      char buf[4];
      size_t len = 0;
      if (xml::decode_entity(s, pos, buf, len)) {
        if (len > 0) out.add(buf, len);
        continue;
      }
    }
    const size_t n = std::min(utf8_length(static_cast<unsigned char>(s[pos])), s.size() - pos);
    out.add(s.data() + pos, n);
    pos += n;
  }
}

void decode_attr(std::string_view attrs, std::string_view name, char* out, size_t size) {
  Text text(out, size, false);
  decode(xml::attr(attrs, name), text);
}

// Project Gutenberg's lists link each book to its own feed,
// ".../ebooks/<number>.opds"; their other links (sorting, authors, "also
// downloaded") are not books.
bool is_book_feed(std::string_view href) {
  const size_t q = href.find('?');
  if (q != std::string_view::npos) href = href.substr(0, q);
  constexpr std::string_view kExt = ".opds";
  if (href.size() <= kExt.size() || href.substr(href.size() - kExt.size()) != kExt) return false;
  href.remove_suffix(kExt.size());
  size_t i = href.size();
  while (i > 0 && is_digit(href[i - 1])) --i;
  if (i == href.size() || i == 0 || href[i - 1] != '/') return false;
  constexpr std::string_view kDir = "/ebooks";
  const std::string_view dir = href.substr(0, i - 1);
  return dir.size() >= kDir.size() && dir.substr(dir.size() - kDir.size()) == kDir;
}

// Several EPUBs of one book: plain EPUB 3 with pictures suits the reader
// best; Standard Ebooks' "compatible" one over its "advanced" one.
int epub_rank(std::string_view href, std::string_view title) {
  int rank = 10;
  if (contains(href, "epub3")) rank += 4;
  if (contains(href, "noimages")) {
    rank -= 4;
  } else if (contains(href, "images")) {
    rank += 2;
  }
  if (contains(title, "ompatible")) rank += 3;
  if (contains(title, "dvanced") || contains(href, "advanced")) rank -= 3;
  if (contains(href, "kepub")) rank -= 6;
  return rank;
}

}  // namespace

void OpdsParser::begin(OpdsFeed& feed, const char* base_url) {
  feed_ = &feed;
  feed.count = 0;
  feed.dropped = 0;
  feed.next[0] = '\0';
  textfile::copy(base_url, base_, sizeof(base_));
  mode_ = Mode::Text;
  tag_len_ = 0;
  quote_ = 0;
  text_len_ = 0;
  field_ = Field::None;
  field_depth_ = 0;
  field_html_ = false;
  atom_ = latin1_ = in_entry_ = in_author_ = has_author_ = false;
}

void OpdsParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) put(data[i]);
}

bool OpdsParser::finish() { return atom_; }

void OpdsParser::put(char c) {
  switch (mode_) {
    case Mode::Text:
      if (c == '<') {
        mode_ = Mode::Tag;
        tag_len_ = 0;
        quote_ = 0;
      } else {
        text_char(c);
      }
      return;
    case Mode::Tag:
      if (quote_ != 0) {
        if (c == quote_) quote_ = 0;
      } else if (c == '>') {
        end_tag();
        mode_ = Mode::Text;
        return;
      } else if ((c == '"' || c == '\'') && tag_len_ > 0 && tag_[0] != '!') {
        quote_ = c;
      }
      if (tag_len_ + 1 < kTagBytes) tag_[tag_len_++] = c;
      if (tag_len_ == 3 && std::memcmp(tag_, "!--", 3) == 0) {
        mode_ = Mode::Comment;
        tail_[0] = tail_[1] = 0;
      } else if (tag_len_ == 8 && std::memcmp(tag_, "![CDATA[", 8) == 0) {
        mode_ = Mode::Cdata;
        tail_[0] = tail_[1] = 0;
      }
      return;
    case Mode::Comment:
    case Mode::Cdata:
      if (c == '>' && tail_[0] == (mode_ == Mode::Comment ? '-' : ']') && tail_[1] == tail_[0]) {
        // The section's "]]" went into the text with the rest: take it back.
        if (mode_ == Mode::Cdata && text_len_ >= 2 && text_[text_len_ - 1] == ']' && text_[text_len_ - 2] == ']')
          text_len_ -= 2;
        mode_ = Mode::Text;
        return;
      }
      tail_[0] = tail_[1];
      tail_[1] = c;
      if (mode_ == Mode::Cdata) {
        // Raw text: escape what take_text() would otherwise decode or strip.
        if (c == '&' || c == '<') {
          for (const char* e = c == '&' ? "&amp;" : "&lt;"; *e != '\0'; ++e) text_char(*e);
        } else {
          text_char(c);
        }
      }
      return;
  }
}

void OpdsParser::text_char(char c) {
  if (field_ == Field::None) return;
  const auto b = static_cast<unsigned char>(c);
  if (latin1_ && b >= 0x80) {
    // ISO-8859-1 maps byte for byte onto the first 256 code points.
    if (text_len_ + 2 < kTextBytes) {
      text_[text_len_++] = static_cast<char>(0xC0 | (b >> 6));
      text_[text_len_++] = static_cast<char>(0x80 | (b & 0x3F));
    }
    return;
  }
  if (text_len_ + 1 < kTextBytes) text_[text_len_++] = c;
}

void OpdsParser::end_tag() {
  std::string_view tag(tag_, tag_len_);
  if (tag.empty() || tag[0] == '!') return;  // a declaration
  if (tag[0] == '?') {
    // <?xml version="1.0" encoding="..."?>
    if (tag.substr(0, 4) == "?xml") {
      char encoding[16];
      decode_attr(tag.substr(4), "encoding", encoding, sizeof(encoding));
      for (char* p = encoding; *p != '\0'; ++p) *p = (*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p;
      latin1_ = std::strcmp(encoding, "iso-8859-1") == 0 || std::strcmp(encoding, "latin1") == 0 ||
                std::strcmp(encoding, "windows-1252") == 0;
    }
    return;
  }
  const bool closing = tag[0] == '/';
  if (closing) tag.remove_prefix(1);
  bool self_closing = false;
  while (!tag.empty() && is_space(tag.back())) tag.remove_suffix(1);
  if (!tag.empty() && tag.back() == '/') {
    self_closing = true;
    tag.remove_suffix(1);
  }
  size_t end = 0;
  while (end < tag.size() && !is_space(tag[end])) ++end;
  std::string_view name = tag.substr(0, end);
  if (const size_t colon = name.rfind(':'); colon != std::string_view::npos) name.remove_prefix(colon + 1);
  if (closing) {
    close(name);
    return;
  }
  open(name, tag.substr(end));
  if (self_closing) close(name);
}

void OpdsParser::open(std::string_view name, std::string_view attrs) {
  if (field_ != Field::None) {
    // XHTML inside a summary: its paragraphs and breaks part words.
    ++field_depth_;
    text_char(' ');
    return;
  }
  if (name == "feed") atom_ = true;
  if (name == "link") return link(attrs);
  if (name == "entry") {
    in_entry_ = true;
    in_author_ = has_author_ = false;
    entry_ = {};
    content_[0] = '\0';
    return;
  }
  if (!in_entry_) return;
  if (name == "author") {
    in_author_ = true;
    return;
  }
  Field f = Field::None;
  if (name == "title") {
    f = Field::Title;
  } else if (name == "name" && in_author_) {
    f = Field::AuthorName;
  } else if (name == "summary") {
    f = Field::Summary;
  } else if (name == "content") {
    f = Field::Content;
  } else if (name == "language") {
    f = Field::Language;
  }
  if (f == Field::None) return;
  field_ = f;
  field_depth_ = 0;
  text_len_ = 0;
  // Escaped HTML ("&lt;p&gt;") turns into markup once decoded: leave it out.
  field_html_ = xml::attr(attrs, "type") == "html";
}

void OpdsParser::close(std::string_view name) {
  if (field_ != Field::None) {
    if (field_depth_ > 0) {
      --field_depth_;
      text_char(' ');
      return;
    }
    switch (field_) {
      case Field::Title:
        take_text(entry_.title, sizeof(entry_.title), false);
        break;
      case Field::AuthorName:
        if (!has_author_) {
          char raw[sizeof(entry_.author)];
          take_text(raw, sizeof(raw), false);
          natural_name(raw, entry_.author, sizeof(entry_.author));
          has_author_ = entry_.author[0] != '\0';
        }
        break;
      case Field::Summary:
        take_text(entry_.summary, sizeof(entry_.summary), field_html_);
        break;
      case Field::Content:
        take_text(content_, sizeof(content_), field_html_);
        break;
      case Field::Language:
        take_text(entry_.language, sizeof(entry_.language), false);
        break;
      case Field::None:
        break;
    }
    field_ = Field::None;
    return;
  }
  if (name == "author") in_author_ = false;
  if (name == "entry" && in_entry_) end_entry();
}

void OpdsParser::link(std::string_view attrs) {
  char rel[64], type[64], href[256];
  decode_attr(attrs, "rel", rel, sizeof(rel));
  decode_attr(attrs, "type", type, sizeof(type));
  decode_attr(attrs, "href", href, sizeof(href));
  if (href[0] == '\0') return;
  if (!in_entry_) {
    if (std::strcmp(rel, "next") == 0) resolve_url(base_, href, feed_->next, sizeof(feed_->next));
    return;
  }
  const std::string_view r(rel), t(type);
  if (t.substr(0, 20) == "application/epub+zip" && (contains(r, "acquisition") || r == "enclosure")) {
    char title[64];
    decode_attr(attrs, "title", title, sizeof(title));
    const int rank = epub_rank(href, title);
    if (rank > entry_.epub_rank && resolve_url(base_, href, entry_.epub, sizeof(entry_.epub))) {
      entry_.epub_rank = static_cast<int8_t>(rank);
      entry_.size = textfile::to_uint(xml::attr(attrs, "length"));
    }
  } else if (t.substr(0, 20) == "application/atom+xml" && entry_.detail[0] == '\0' && is_book_feed(href)) {
    resolve_url(base_, href, entry_.detail, sizeof(entry_.detail));
  }
}

void OpdsParser::end_entry() {
  in_entry_ = false;
  if (entry_.author[0] == '\0' && entry_.epub[0] == '\0') {
    // Gutenberg's lists give the author as the entry's content.
    textfile::copy(content_, entry_.author, sizeof(entry_.author));
  } else if (entry_.summary[0] == '\0') {
    textfile::copy(content_, entry_.summary, sizeof(entry_.summary));
  }
  if (entry_.title[0] == '\0' || (entry_.epub[0] == '\0' && entry_.detail[0] == '\0')) return;  // not a book
  if (feed_->count < feed_->capacity) {
    feed_->entries[feed_->count++] = entry_;
  } else {
    ++feed_->dropped;
  }
}

void OpdsParser::take_text(char* out, size_t size, bool strip_markup) {
  std::string_view raw(text_, text_len_);
  if (strip_markup) {
    // Escaped HTML: the XML's references give the markup, whose own
    // references ("&amp;amp;") then give the text.
    Text html(html_, sizeof(html_), false);
    decode(raw, html);
    raw = html_;
  }
  Text text(out, size, strip_markup);
  decode(raw, text);
  text_len_ = 0;
}

bool resolve_url(const char* base, std::string_view href, char* out, size_t size) {
  const std::string_view b(base);
  const size_t scheme_end = b.find("://");
  std::string_view prefix;
  if (href.find("://") != std::string_view::npos) {
    prefix = {};
  } else if (scheme_end == std::string_view::npos) {
    prefix = {};  // nothing to resolve against
  } else if (href.substr(0, 2) == "//") {
    prefix = b.substr(0, scheme_end + 1);  // "https:"
  } else if (!href.empty() && href[0] == '/') {
    prefix = b.substr(0, b.find('/', scheme_end + 3));  // "https://host"
  } else {
    // Relative to the base's folder, without its query.
    std::string_view path = b.substr(0, b.find('?'));
    const size_t slash = path.rfind('/');
    prefix = slash > scheme_end + 2 ? path.substr(0, slash + 1) : path;
    if (slash <= scheme_end + 2) {
      // "https://host" with no path: the folder is the root.
      const int n = std::snprintf(out, size, "%.*s/%.*s", static_cast<int>(prefix.size()), prefix.data(),
                                  static_cast<int>(href.size()), href.data());
      return n >= 0 && static_cast<size_t>(n) < size;
    }
  }
  const int n = std::snprintf(out, size, "%.*s%.*s", static_cast<int>(prefix.size()), prefix.data(),
                              static_cast<int>(href.size()), href.data());
  return n >= 0 && static_cast<size_t>(n) < size;
}

void natural_name(std::string_view name, char* out, size_t size) {
  // Parts between commas, without dates ("1775-1817") or empty ones.
  std::string_view parts[2];
  int count = 0;
  while (!name.empty() && count < 2) {
    const size_t comma = name.find(',');
    std::string_view part = name.substr(0, comma);
    name = comma == std::string_view::npos ? std::string_view{} : name.substr(comma + 1);
    while (!part.empty() && is_space(part.front())) part.remove_prefix(1);
    while (!part.empty() && is_space(part.back())) part.remove_suffix(1);
    bool date = false;
    for (char c : part) date = date || is_digit(c);
    if (!part.empty() && !date) parts[count++] = part;
  }
  if (count == 2) {
    std::snprintf(out, size, "%.*s %.*s", static_cast<int>(parts[1].size()), parts[1].data(),
                  static_cast<int>(parts[0].size()), parts[0].data());
  } else {
    textfile::copy(count == 1 ? parts[0] : std::string_view{}, out, size);
  }
}

bool url_encode_query(std::string_view words, char* out, size_t size) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  size_t len = 0;
  for (char c : words) {
    const auto b = static_cast<unsigned char>(c);
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) ||
                       (c != '\0' && std::strchr("-_.~", c) != nullptr);
    const size_t n = plain || c == ' ' ? 1 : 3;
    if (len + n + 1 > size) return false;
    if (c == ' ') {
      out[len++] = '+';
    } else if (plain) {
      out[len++] = c;
    } else {
      out[len++] = '%';
      out[len++] = kHex[b >> 4];
      out[len++] = kHex[b & 15];
    }
  }
  if (size == 0) return false;
  out[len] = '\0';
  return true;
}

}  // namespace vellum

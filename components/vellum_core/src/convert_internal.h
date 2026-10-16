// Pieces the converters share: writing the web page, reading a file a
// window at a time.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "vellum/convert.h"
#include "vellum/hal.h"
#include "vellum/xml.h"

namespace vellum::convert {

inline char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Whether s ends in `ext` (lowercase), in any case.
inline bool ends_with_lower(std::string_view s, std::string_view ext) {
  if (s.size() < ext.size()) return false;
  s = s.substr(s.size() - ext.size());
  for (size_t i = 0; i < ext.size(); ++i) {
    if (ascii_lower(s[i]) != ext[i]) return false;
  }
  return true;
}

inline uint32_t be16(const uint8_t* p) { return static_cast<uint32_t>(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
         p[3];
}

// The file extension a picture's first bytes call for, or nullptr when it
// is not a picture Vellum can show (JPEG, PNG) or keep (GIF, BMP).
inline const char* picture_ext(const uint8_t* d, uint32_t len) {
  if (len >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return "jpg";
  if (len >= 8 && std::memcmp(d, "\x89PNG", 4) == 0) return "png";
  if (len >= 6 && std::memcmp(d, "GIF8", 4) == 0) return "gif";
  if (len >= 2 && d[0] == 'B' && d[1] == 'M') return "bmp";
  return nullptr;
}

// The web page being written, through a small buffer. Created only once
// the converter is ready to write, because the storage has one writer at a
// time; finish() keeps it, or throws it away when anything went wrong.
class Page {
 public:
  explicit Page(Job& job) : storage_(*job.storage), w_(job.storage->create(job.out_path)), ok_(w_ != nullptr) {}
  ~Page() { finish(false); }
  Page(const Page&) = delete;
  Page& operator=(const Page&) = delete;

  bool ok() const { return ok_; }

  void raw(std::string_view s) {
    while (!s.empty() && ok_) {
      if (n_ == sizeof(buf_)) flush();
      const size_t k = std::min<size_t>(s.size(), sizeof(buf_) - n_);
      std::memcpy(buf_ + n_, s.data(), k);
      n_ += k;
      s.remove_prefix(k);
    }
  }
  void raw(char c) { raw(std::string_view(&c, 1)); }
  // UTF-8 text, escaped.
  void text(std::string_view s) {
    size_t from = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      const char* esc = s[i] == '&'   ? "&amp;"
                        : s[i] == '<' ? "&lt;"
                        : s[i] == '>' ? "&gt;"
                        : s[i] == '"' ? "&quot;"
                                      : nullptr;
      if (esc == nullptr) continue;
      raw(s.substr(from, i - from));
      raw(esc);
      from = i + 1;
    }
    raw(s.substr(from));
  }
  void code_point(uint32_t cp) {
    if (cp == '&' || cp == '<' || cp == '>' || cp == '"') {
      const char c = static_cast<char>(cp);
      text(std::string_view(&c, 1));
      return;
    }
    char u[4];
    raw(std::string_view(u, xml::encode_utf8(cp, u)));
  }

  // The head: title, author and language (UTF-8; any may be empty), the
  // cover picture's file name, and whether <vellum-chapter> marks give the contents.
  void start(std::string_view title, std::string_view author, std::string_view language, std::string_view cover,
             bool marks) {
    raw("<!DOCTYPE html>\n<html");
    if (!language.empty()) {
      raw(" lang=\"");
      text(language);
      raw('"');
    }
    raw("><head><meta charset=\"utf-8\"><title>");
    text(title);
    raw("</title>");
    if (!author.empty()) {
      raw("<meta name=\"author\" content=\"");
      text(author);
      raw("\">");
    }
    if (!cover.empty()) {
      raw("<meta name=\"cover\" content=\"");
      text(cover);
      raw("\">");
    }
    if (marks) raw("<meta name=\"vellum-contents\" content=\"marks\">");
    raw("</head><body>\n");
  }
  // A contents entry, where a chapter begins.
  void chapter(std::string_view title, int depth) {
    char d[24];
    raw("<vellum-chapter depth=\"");
    raw(std::string_view(d, static_cast<size_t>(std::snprintf(d, sizeof(d), "%d", depth < 0 ? 0 : depth))));
    raw("\" title=\"");
    text(title);
    raw("\"/>\n");
  }
  void end() { raw("\n</body></html>\n"); }

  // Writes out the buffer; false once anything failed.
  bool flush() {
    if (ok_ && n_ > 0) ok_ = w_->write(buf_, static_cast<uint32_t>(n_));
    n_ = 0;
    return ok_;
  }
  // Keeps the page when `keep` and all went well. Returns whether it was kept.
  bool finish(bool keep) {
    if (w_ == nullptr) return false;
    if (keep) flush();
    const bool kept = storage_.finish(w_, keep && ok_) && keep && ok_;
    w_ = nullptr;
    ok_ = false;
    return kept;
  }

 private:
  Storage& storage_;
  Writer* w_;
  bool ok_;
  char buf_[2048];
  size_t n_ = 0;
};

// A file read byte by byte through a window.
class Bytes {
 public:
  Bytes(File& f, uint8_t* buf, uint32_t cap, uint32_t from = 0)
      : f_(f), buf_(buf), cap_(cap), end_(f.size()), at_(from) {}
  Bytes(File& f, uint8_t* buf, uint32_t cap, uint32_t from, uint32_t end)
      : f_(f), buf_(buf), cap_(cap), end_(std::min<uint32_t>(end, f.size())), at_(from) {}

  // The next byte, or -1 at the end (or on a read error).
  int get() {
    if (i_ == n_ && !fill()) return -1;
    return buf_[i_++];
  }
  int peek() {
    if (i_ == n_ && !fill()) return -1;
    return buf_[i_];
  }
  uint32_t pos() const { return at_ - (n_ - i_); }
  bool failed() const { return failed_; }

 private:
  bool fill() {
    if (at_ >= end_) return false;
    const uint32_t want = std::min<uint32_t>(cap_, end_ - at_);
    if (f_.read_at(at_, buf_, want) != static_cast<int32_t>(want)) {
      failed_ = true;
      return false;
    }
    at_ += want;
    n_ = want;
    i_ = 0;
    return true;
  }

  File& f_;
  uint8_t* buf_;
  uint32_t cap_;
  uint32_t end_;
  uint32_t at_;
  uint32_t n_ = 0;
  uint32_t i_ = 0;
  bool failed_ = false;
};

// Walks the tags of a file too large to hold, a window of `cap` bytes at a
// time. Calls fn(tag, text, start, end) for each tag: `text` is the raw text
// since the previous tag, [start, end) where the tag is in the file. The
// views stay valid only during the call. fn returns false to stop.
template <typename Fn>
bool walk_tags(File& file, uint32_t from, uint8_t* buf, uint32_t cap, Fn&& fn) {
  const uint32_t size = file.size();
  uint32_t w = from;
  while (w < size) {
    const uint32_t want = std::min<uint32_t>(size - w, cap);
    if (file.read_at(w, buf, want) != static_cast<int32_t>(want)) return false;
    const std::string_view s(reinterpret_cast<const char*>(buf), want);
    size_t pos = 0;
    size_t before = 0;
    for (;;) {
      size_t start;
      xml::Tag tag;
      before = pos;
      if (!xml::next_tag(s, pos, tag, start)) break;
      if (!fn(tag, s.substr(before, start - before), w + static_cast<uint32_t>(start),
              w + static_cast<uint32_t>(pos))) {
        return true;
      }
    }
    if (w + want >= size) {
      // The text after the last tag.
      if (before < s.size()) {
        xml::Tag none{{}, {}, false, false};
        fn(none, s.substr(before), size, size);
      }
      break;
    }
    // Go on from the text after the last whole tag, so a tag or a run of
    // text cut by the window's end is seen whole in the next one.
    if (before > 0) {
      w += static_cast<uint32_t>(before);
    } else {
      const size_t lt = s.find('<');
      w += lt == std::string_view::npos || lt == 0 ? want : static_cast<uint32_t>(lt);
    }
  }
  return true;
}

// Text of an XML element's content: entities decoded into UTF-8, appended
// to out[n, cap). Returns the new length.
inline size_t append_xml_text(std::string_view s, char* out, size_t n, size_t cap) {
  for (size_t i = 0; i < s.size() && n + 4 < cap;) {
    size_t len = 0;
    if (s[i] == '&' && xml::decode_entity(s, i, out + n, len)) {
      n += len;
    } else {
      out[n++] = s[i++];
    }
  }
  return n;
}

}  // namespace vellum::convert

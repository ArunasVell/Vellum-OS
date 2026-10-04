#include "vellum/xml.h"

#include <cstring>

namespace vellum::xml {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

struct Named {
  const char* name;
  uint32_t cp;
};
constexpr Named kNamed[] = {
    {"amp", '&'},       {"lt", '<'},       {"gt", '>'},        {"quot", '"'},     {"apos", '\''},
    {"nbsp", 0xA0},     {"shy", 0xAD},     {"ndash", 0x2013},  {"mdash", 0x2014}, {"lsquo", 0x2018},
    {"rsquo", 0x2019},  {"sbquo", 0x201A}, {"ldquo", 0x201C},  {"rdquo", 0x201D}, {"bdquo", 0x201E},
    {"hellip", 0x2026}, {"laquo", 0xAB},   {"raquo", 0xBB},    {"copy", 0xA9},    {"bull", 0x2022},
    {"middot", 0xB7},   {"euro", 0x20AC},  {"thinsp", 0x2009}, {"ensp", 0x2002},  {"emsp", 0x2003},
};

}  // namespace

size_t encode_utf8(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

bool name_is(std::string_view name, std::string_view lower_name) {
  if (name.size() != lower_name.size()) return false;
  for (size_t i = 0; i < name.size(); ++i) {
    if (lower(name[i]) != lower_name[i]) return false;
  }
  return true;
}

std::string_view attr(std::string_view attrs, std::string_view name) {
  size_t i = 0;
  while (i < attrs.size()) {
    while (i < attrs.size() && (is_space(attrs[i]) || attrs[i] == '/')) ++i;
    const size_t key_start = i;
    while (i < attrs.size() && attrs[i] != '=' && !is_space(attrs[i])) ++i;
    std::string_view key = attrs.substr(key_start, i - key_start);
    if (const size_t colon = key.rfind(':'); colon != std::string_view::npos) key.remove_prefix(colon + 1);
    while (i < attrs.size() && is_space(attrs[i])) ++i;
    if (i >= attrs.size() || attrs[i] != '=') continue;
    ++i;
    while (i < attrs.size() && is_space(attrs[i])) ++i;
    if (i >= attrs.size()) break;
    const char quote = attrs[i];
    size_t value_start;
    size_t value_end;
    if (quote == '"' || quote == '\'') {
      value_start = ++i;
      while (i < attrs.size() && attrs[i] != quote) ++i;
      value_end = i++;
    } else {
      value_start = i;
      while (i < attrs.size() && !is_space(attrs[i])) ++i;
      value_end = i;
    }
    if (key == name) return attrs.substr(value_start, value_end - value_start);
  }
  return {};
}

bool next_tag(std::string_view s, size_t& pos, Tag& tag, size_t& start) {
  for (;;) {
    const size_t lt = s.find('<', pos);
    if (lt == std::string_view::npos) return false;
    std::string_view rest = s.substr(lt);
    const char* close = nullptr;
    if (rest.starts_with("<!--")) {
      close = "-->";
    } else if (rest.starts_with("<![CDATA[")) {
      close = "]]>";
    } else if (rest.starts_with("<?") || rest.starts_with("<!")) {
      close = ">";
    }
    if (close != nullptr) {
      const size_t end = s.find(close, lt);
      if (end == std::string_view::npos) return false;
      pos = end + std::strlen(close);
      continue;
    }
    // Find the '>' that ends the tag, skipping quoted attribute values.
    size_t i = lt + 1;
    char quote = 0;
    for (; i < s.size(); ++i) {
      if (quote) {
        if (s[i] == quote) quote = 0;
      } else if (s[i] == '"' || s[i] == '\'') {
        quote = s[i];
      } else if (s[i] == '>') {
        break;
      }
    }
    if (i >= s.size()) return false;
    std::string_view inner = s.substr(lt + 1, i - lt - 1);
    tag.closing = !inner.empty() && inner[0] == '/';
    if (tag.closing) inner.remove_prefix(1);
    tag.self_closing = !inner.empty() && inner.back() == '/';
    if (tag.self_closing) inner.remove_suffix(1);
    size_t n = 0;
    while (n < inner.size() && !is_space(inner[n])) ++n;
    std::string_view name = inner.substr(0, n);
    if (const size_t colon = name.rfind(':'); colon != std::string_view::npos) name.remove_prefix(colon + 1);
    tag.name = name;
    tag.attrs = inner.substr(n);
    start = lt;
    pos = i + 1;
    return true;
  }
}

bool decode_entity(std::string_view s, size_t& pos, char* out, size_t& len) {
  const size_t semi = s.find(';', pos);
  if (semi == std::string_view::npos || semi - pos > 10) return false;
  const std::string_view body = s.substr(pos + 1, semi - pos - 1);
  uint32_t cp = 0;
  if (body.size() >= 2 && body[0] == '#') {
    const bool hex = body[1] == 'x' || body[1] == 'X';
    for (size_t i = hex ? 2 : 1; i < body.size(); ++i) {
      const char c = body[i];
      int d;
      if (c >= '0' && c <= '9') {
        d = c - '0';
      } else if (hex && lower(c) >= 'a' && lower(c) <= 'f') {
        d = lower(c) - 'a' + 10;
      } else {
        return false;
      }
      cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(d);
      if (cp > 0x10FFFF) return false;
    }
    if (cp == 0) return false;
  } else {
    bool found = false;
    for (const Named& n : kNamed) {
      if (body == n.name) {
        cp = n.cp;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  pos = semi + 1;
  len = cp == 0xAD ? 0 : encode_utf8(cp, out);  // soft hyphens dropped: Vellum hyphenates by itself
  return true;
}

}  // namespace vellum::xml

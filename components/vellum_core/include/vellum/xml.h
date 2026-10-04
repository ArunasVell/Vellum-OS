// Just enough XML for EPUB: walks tags and text without building a tree,
// and decodes character references. Namespace prefixes are ignored.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vellum::xml {

struct Tag {
  std::string_view name;   // local name, without prefix, as written
  std::string_view attrs;  // raw text after the name
  bool closing;            // </name>
  bool self_closing;       // <name/>
};

// Value of attribute `name` (local name) in a tag's attrs, or empty.
std::string_view attr(std::string_view attrs, std::string_view name);

// Case-insensitive comparison of an ASCII tag name.
bool name_is(std::string_view name, std::string_view lower);

// Finds the next tag at or after `pos`, skipping comments, processing
// instructions and CDATA. Sets `start` to its '<' and `pos` past its '>'.
// Returns false at the end of the text.
bool next_tag(std::string_view s, size_t& pos, Tag& tag, size_t& start);

// Decodes the character reference at s[pos] ('&'): writes its UTF-8 into out
// (at least 4 bytes), sets len (0 for a soft hyphen, which is dropped) and
// advances pos. Returns false, leaving pos, when it is not a known reference.
bool decode_entity(std::string_view s, size_t& pos, char* out, size_t& len);

// Appends the UTF-8 encoding of cp to out; returns the bytes written (1-4).
size_t encode_utf8(uint32_t cp, char* out);

}  // namespace vellum::xml

// Character encodings of plain-text, FB2 and HTML books: finding which one a
// file uses and turning it into UTF-8, the only encoding the rest of Vellum
// knows.
#pragma once

#include <cstdint>
#include <string_view>

namespace vellum::charset {

enum class Encoding : uint8_t {
  Utf8,
  Utf16LE,
  Utf16BE,
  Windows1257,  // Baltic: Lithuanian and Latvian
  Windows1252,  // Western European
  Iso8859_13,   // Baltic, older
  Windows1251,  // Cyrillic, common in FB2 files
  Latin1,       // ISO-8859-1
};

// Guesses the encoding from the start of a file: a byte order mark, then
// valid UTF-8 (plain ASCII counts), then UTF-16 without a mark, then which
// one-byte code page fits the letters best. Sets bom to the length of the
// byte order mark, which is not text.
Encoding detect(const uint8_t* data, uint32_t len, uint32_t& bom);

// The encoding named in an XML or HTML declaration, e.g. "windows-1257" or
// "UTF-8" (any case). Returns false for names it does not know.
bool from_name(std::string_view name, Encoding& out);

// Bytes per code unit: 2 for UTF-16, else 1.
inline uint32_t unit(Encoding e) { return (e == Encoding::Utf16LE || e == Encoding::Utf16BE) ? 2 : 1; }

// Converts `len` bytes to UTF-8 in out, which holds `cap` bytes; 3 * len
// always suffices. Malformed input becomes U+FFFD; a sequence cut off at the
// end is dropped. Returns the bytes written (no NUL is added).
uint32_t to_utf8(Encoding e, const uint8_t* in, uint32_t len, char* out, uint32_t cap);

}  // namespace vellum::charset

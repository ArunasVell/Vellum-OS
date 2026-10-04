// Liang's hyphenation algorithm (the one TeX uses) over pattern tables
// generated from the hyph-utf8 project by tools/hyphconvert.py.
#pragma once

#include <cstdint>
#include <string_view>

namespace vellum {

struct HyphenPattern {
  const char* letters;  // lowercase UTF-8; '.' marks a word edge
  const char* digits;   // one digit per gap between letters, edges included
};

struct Hyphenator {
  const HyphenPattern* patterns;  // sorted by letters, bytewise
  uint32_t pattern_count;
  const char* const* exceptions;  // whole words with hyphens, sorted without them
  uint32_t exception_count;
  uint8_t left_min;   // fewest letters before a hyphen
  uint8_t right_min;  // fewest letters after a hyphen
};

namespace hyphenation {
extern const Hyphenator en_us;
extern const Hyphenator lt;
}  // namespace hyphenation

// Rules for a BCP 47 language tag ("en", "en-GB", "lt", ...), or nullptr
// when there are none for that language.
const Hyphenator* hyphenator_for(std::string_view language);

// Finds where `word` may be hyphenated. Writes the byte offsets of the
// allowed breaks (a hyphen goes before that byte) to `breaks`, in increasing
// order, and returns how many there are (at most `max`). Leading and trailing
// punctuation is ignored; words with digits or other non-letters inside get
// no breaks.
int hyphenate(const Hyphenator& h, std::string_view word, uint16_t* breaks, int max);

}  // namespace vellum

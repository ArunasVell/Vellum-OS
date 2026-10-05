#include "vellum/hyphenation.h"

#include <algorithm>

#include "vellum/text.h"

namespace vellum {
namespace {

constexpr int kMaxLetters = 48;

// Lowercase for the scripts the fonts cover: ASCII, Latin-1, Latin Extended-A.
uint32_t to_lower(uint32_t c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
  if (c >= 0x100 && c <= 0x17F) {
    // Pairs are upper/lower, upper first, except two runs that start on an odd code point.
    const bool odd_run = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
    if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
    if (odd_run ? (c & 1) : !(c & 1)) return c + 1;
  }
  return c;
}

bool is_letter(uint32_t c) {
  c = to_lower(c);
  return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || (c >= 0x100 && c <= 0x17F);
}

size_t put_utf8(uint32_t c, char* out) {
  if (c < 0x80) {
    out[0] = static_cast<char>(c);
    return 1;
  }
  if (c < 0x800) {
    out[0] = static_cast<char>(0xC0 | (c >> 6));
    out[1] = static_cast<char>(0x80 | (c & 0x3F));
    return 2;
  }
  out[0] = static_cast<char>(0xE0 | (c >> 12));
  out[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
  out[2] = static_cast<char>(0x80 | (c & 0x3F));
  return 3;
}

// The first pattern in [lo, hi) not before `key` in the table's (bytewise) order.
const HyphenPattern* first_not_before(const HyphenPattern* lo, const HyphenPattern* hi, std::string_view key) {
  return std::lower_bound(lo, hi, key,
                          [](const HyphenPattern& a, std::string_view k) { return std::string_view(a.letters) < k; });
}

// Compares an exception entry, ignoring its hyphens, with a lowercase word.
int compare_exception(const char* entry, std::string_view word) {
  size_t i = 0;
  for (const char* e = entry; *e; ++e) {
    if (*e == '-') continue;
    if (i == word.size()) return 1;
    const auto a = static_cast<unsigned char>(*e);
    const auto b = static_cast<unsigned char>(word[i++]);
    if (a != b) return a < b ? -1 : 1;
  }
  return i == word.size() ? 0 : -1;
}

}  // namespace

const Hyphenator* hyphenator_for(std::string_view language) {
  const std::string_view primary = language.substr(0, language.find('-'));
  if (primary == "en") return &hyphenation::en_us;
  if (primary == "lt") return &hyphenation::lt;
  return nullptr;
}

int hyphenate(const Hyphenator& h, std::string_view word, uint16_t* breaks, int max) {
  // Letters of the word (lowercased) and the byte offset where each starts.
  uint32_t letters[kMaxLetters];
  uint16_t starts[kMaxLetters];
  int n = 0;
  size_t i = 0;
  while (i < word.size()) {
    const size_t at = i;
    const uint32_t c = decode_utf8(word, i);
    if (is_letter(c)) {
      if (n == kMaxLetters) return 0;
      letters[n] = to_lower(c);
      starts[n] = static_cast<uint16_t>(at);
      ++n;
    } else if (n > 0) {
      // Only trailing punctuation may follow the letters.
      size_t j = i;
      while (j < word.size()) {
        if (is_letter(decode_utf8(word, j))) return 0;
      }
      break;
    }
  }
  if (n < h.left_min + h.right_min) return 0;

  // Lowercase UTF-8 with word-edge dots, and where each letter starts in it.
  char key[kMaxLetters * 3 + 2];
  uint16_t pos[kMaxLetters + 3];
  size_t len = 0;
  pos[0] = 0;
  key[len++] = '.';
  for (int k = 0; k < n; ++k) {
    pos[k + 1] = static_cast<uint16_t>(len);
    len += put_utf8(letters[k], key + len);
  }
  pos[n + 1] = static_cast<uint16_t>(len);
  key[len++] = '.';
  pos[n + 2] = static_cast<uint16_t>(len);
  const int units = n + 2;  // letters plus the two dots

  // values[g] is the score of the gap before unit g.
  uint8_t values[kMaxLetters + 3] = {};

  const std::string_view bare(key + 1, len - 2);
  const char* const* ex_end = h.exceptions + h.exception_count;
  const char* const* ex = std::lower_bound(
      h.exceptions, ex_end, bare, [](const char* e, std::string_view w) { return compare_exception(e, w) < 0; });
  if (ex != ex_end && compare_exception(*ex, bare) == 0) {
    int letter = 0;
    for (const char* e = *ex; *e; ++e) {
      if (*e == '-') {
        values[letter + 1] = 1;
      } else if ((static_cast<unsigned char>(*e) & 0xC0) != 0x80) {
        ++letter;
      }
    }
  } else {
    for (int a = 0; a < units; ++a) {
      // The patterns starting with the piece from a to b, narrowed as b grows.
      const HyphenPattern* lo = h.patterns;
      const HyphenPattern* hi = h.patterns + h.pattern_count;
      for (int b = a + 1; b <= units; ++b) {
        const std::string_view part(key + pos[a], pos[b] - pos[a]);
        const auto starts = [&](const HyphenPattern& x) { return std::string_view(x.letters).starts_with(part); };
        lo = first_not_before(lo, hi, part);  // the first starting with `part`, if any does
        if (lo == hi || !starts(*lo)) break;  // none: no longer piece matches either
        hi = std::partition_point(lo, hi, starts);
        if (lo->letters[part.size()] != '\0') continue;  // only longer ones: not `part` itself
        for (int g = 0; g <= b - a; ++g) {
          values[a + g] = std::max<uint8_t>(values[a + g], static_cast<uint8_t>(lo->digits[g] - '0'));
        }
      }
    }
  }

  // An odd score allows a break; gap g sits before letter g (1-based units).
  int count = 0;
  for (int g = 1 + h.left_min; g <= 1 + n - h.right_min && count < max; ++g) {
    if (values[g] & 1) breaks[count++] = starts[g - 1];
  }
  return count;
}

}  // namespace vellum

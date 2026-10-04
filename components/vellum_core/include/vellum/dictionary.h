// Dictionaries on the memory card, for looking up a word while reading.
//
// They are StarDict dictionaries in the card's "dictionaries" folder (or a
// folder inside it): a .ifo file, its .idx word list and its .dict text
// (or .dict.dz, a dictzip: gzip in chunks that can be read one at a time).
// A dictionary's language comes from its folder or file name ("en",
// "lt-en", "english"...); words are looked up in the book's language first.
//
// The word list is never loaded whole: the first lookup in a dictionary
// writes a small index of every Nth word to vellum/dictionaries on the card,
// and each lookup then reads one stretch of the list.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vellum/hal.h"

namespace vellum {

namespace dict {

constexpr size_t kFormBytes = 64;

// Ways a word read on the page may be written in a dictionary, most likely
// first: as it stands without punctuation around it, in lowercase and, for
// English ("en..."), without a common ending (-s, -es, -ed, -ing, 's).
// Returns how many were written to `out`.
int word_forms(std::string_view word, const char* language, char (*out)[kFormBytes], int max);

// Lowercase UTF-8: ASCII, Latin-1 and Latin Extended-A letters (all of
// Lithuanian among them). Other text is copied as it is.
void to_lower(std::string_view s, char* out, size_t size);

// Plain text from a definition's markup (HTML, Pango or XDXF): tags out,
// line breaks kept, character references decoded, spaces collapsed. With
// `plain` the text has no markup: only its spaces are tidied. Returns the
// length written.
size_t markup_to_text(std::string_view in, char* out, size_t size, bool plain = false);

// The language a dictionary is for, from words in its path or name ("en"
// from "dictionaries/en/WordNet.ifo" or "eng-lit.ifo"), or "".
void language_of(std::string_view path, char* out, size_t size);

}  // namespace dict

class Dictionaries {
 public:
  static constexpr int kMax = 16;
  static constexpr const char* kFolder = "dictionaries";
  static constexpr const char* kIndexFolder = "vellum/dictionaries";
  static constexpr int kMaxSamples = 8192;  // words in the index kept in memory

  Dictionaries();
  ~Dictionaries();
  Dictionaries(const Dictionaries&) = delete;
  Dictionaries& operator=(const Dictionaries&) = delete;

  // Finds the dictionaries on the card (none without one).
  void scan(Storage* storage);
  int count() const { return count_; }
  const char* name(int i) const { return entries_[i].name; }
  const char* language(int i) const { return entries_[i].lang; }

  struct Result {
    char headword[dict::kFormBytes] = {};
    char dictionary[64] = {};  // its name
    size_t length = 0;         // of the text written
  };
  // Called before a dictionary's index is first built, which takes a moment.
  using SlowFn = void (*)(void* ctx);

  // Looks `word` up, as written and in its other forms, in the dictionaries
  // for `language` (BCP 47, e.g. "en-GB"), then those for `fallback`, then
  // any other. Writes the definition as plain text to `text`. Returns false
  // when no dictionary has the word.
  bool lookup(std::string_view word, const char* language, const char* fallback, Result& result, char* text, size_t cap,
              SlowFn slow = nullptr, void* ctx = nullptr);

 private:
  struct Entry {
    char base[160];  // path without ".ifo"
    char name[64];
    char lang[8];
    char types[8];  // sametypesequence, or "" when each field names its type
    uint32_t words;
    uint32_t idx_size;
    bool offset64;
    bool dz;  // the text is in .dict.dz
  };

  bool allocate();
  void add(Storage& storage, const char* ifo_path);
  // Makes dictionary i the one whose index is in memory.
  bool load(int i, SlowFn slow, void* ctx);
  bool build_index(File& idx, const Entry& e);
  // The headword at `at` in the word list: its definition's place and the
  // next entry's offset. False at a damaged entry.
  bool read_entry(File& idx, uint32_t at, char* word, uint64_t& offset, uint32_t& size, uint32_t& next);
  // Finds `key` in dictionary loaded_; false when it is not there.
  bool find(const char* key, char* headword, uint64_t& offset, uint32_t& size);
  // Reads `size` bytes of definition data at `offset` into raw_.
  int32_t read_data(uint64_t offset, uint32_t size);
  bool open_dz(File& f);
  bool read_dz_chunk(uint32_t chunk);
  void index_path(const Entry& e, char* out, size_t size) const;

  Storage* storage_ = nullptr;
  Entry* entries_;
  int count_ = 0;

  // The loaded dictionary.
  int loaded_ = -1;
  uint32_t* block_;    // index file image: 4 header words, then samples
  uint32_t* samples_;  // offsets in the word list of every stride_-th word
  int sample_count_ = 0;
  uint32_t stride_ = 1;
  // Its .dict.dz chunks: every kCoarse-th chunk's place in the file.
  static constexpr int kCoarse = 64;
  uint32_t* coarse_;
  uint32_t chunk_len_ = 0;
  uint32_t chunk_count_ = 0;
  uint32_t sizes_at_ = 0;  // where the chunk sizes are listed
  int32_t inflated_ = -1;  // the chunk now in out_
  uint32_t inflated_len_ = 0;
  uint8_t* out_;  // one inflated chunk
  uint8_t* in_;   // compressed input, a piece at a time
  void* inflater_;
  char* raw_;      // a definition as stored
  uint8_t* scan_;  // a stretch of the word list
};

}  // namespace vellum

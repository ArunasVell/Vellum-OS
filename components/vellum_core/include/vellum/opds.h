// Book catalogues on the web (OPDS, which is Atom XML): a parser fed a feed
// piece by piece as it downloads. It builds no tree and keeps only what the
// store shows: each book's title, author, summary and best EPUB download,
// the link to the feed's next page and its search, and (when asked) the
// sections of a catalogue that are lists of more entries.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vellum {

struct OpdsEntry {
  char title[128] = {};
  char author[96] = {};  // "First Last", the first author only
  char summary[640] = {};
  char language[12] = {};  // e.g. "en", or ""
  char detail[256] = {};   // a feed with more about the book and its downloads, or ""
  char epub[256] = {};     // the best EPUB to download, or ""
  uint32_t size = 0;       // that download's length in bytes; 0 when the feed does not say
  int8_t epub_rank = -1;   // how well that EPUB suits the reader, to choose among several
  bool section = false;    // not a book: a list of more entries (a catalogue's section) at `detail`
};

// What a feed held. `entries` is the caller's, `capacity` entries long.
struct OpdsFeed {
  OpdsEntry* entries = nullptr;
  int capacity = 0;
  int count = 0;
  int dropped = 0;      // books that did not fit
  char next[256] = {};  // the next page of the list, or ""
};

class OpdsParser {
 public:
  // Starts a feed fetched from `base_url` (relative links resolve against
  // it). `sections` keeps entries that lead to other lists (a catalogue's
  // navigation); Project Gutenberg's lists hold such links that are not
  // worth showing.
  void begin(OpdsFeed& feed, const char* base_url, bool sections = false);
  // The next piece of the feed, in order; pieces may split anywhere.
  void feed(const char* data, size_t len);
  // After the last piece. Returns false when it was not an Atom feed (an
  // HTML error page, say).
  bool finish();
  // The feed's search: an OpenSearch description, or an address with
  // "{searchTerms}" in it (search_is_template()); "" when it has none.
  const char* search() const { return search_; }
  bool search_is_template() const { return search_template_; }

 private:
  enum class Mode : uint8_t { Text, Tag, Comment, Cdata };
  enum class Field : uint8_t { None, Title, AuthorName, Summary, Content, Language };
  static constexpr size_t kTagBytes = 1024;
  static constexpr size_t kTextBytes = 2048;

  void put(char c);
  void text_char(char c);
  void end_tag();
  void open(std::string_view name, std::string_view attrs);
  void close(std::string_view name);
  void link(std::string_view attrs);
  void end_entry();
  // Decodes the text gathered for the open field into out.
  void take_text(char* out, size_t size, bool strip_markup);

  OpdsFeed* feed_ = nullptr;
  char base_[256] = {};
  Mode mode_ = Mode::Text;
  char tag_[kTagBytes];
  size_t tag_len_ = 0;
  char quote_ = 0;     // inside a quoted attribute value
  char tail_[2] = {};  // the last characters of a comment or CDATA section
  char text_[kTextBytes];
  char html_[kTextBytes];  // escaped HTML, once decoded
  size_t text_len_ = 0;
  Field field_ = Field::None;
  int field_depth_ = 0;      // markup nested in a summary or content
  bool field_html_ = false;  // the field is escaped HTML
  bool atom_ = false;
  bool latin1_ = false;  // the feed says it is ISO-8859-1
  bool in_entry_ = false;
  bool in_author_ = false;
  bool has_author_ = false;
  OpdsEntry entry_;
  char content_[sizeof(OpdsEntry::summary)] = {};
  bool sections_ = false;
  char section_[256] = {};  // the entry's link to another list
  char search_[256] = {};
  bool search_template_ = false;
};

// The address that searches for `words` from an OpenSearch template (its
// {searchTerms} filled in, other {parameters} left empty). False when it
// has no {searchTerms} or does not fit.
bool search_from_template(const char* tmpl, const char* words, char* out, size_t size);

// The Atom (or else any) search template in an OpenSearch description,
// resolved against `base`. False when there is none.
bool opensearch_template(std::string_view xml, const char* base, char* out, size_t size);

// Resolves `href` (absolute, "//host/..", "/path" or relative) against
// `base` into out. Returns false when it does not fit.
bool resolve_url(const char* base, std::string_view href, char* out, size_t size);

// "Austen, Jane, 1775-1817" as people say it: "Jane Austen".
void natural_name(std::string_view name, char* out, size_t size);

// Percent-encodes words for a URL query ("war & peace" -> "war+%26+peace").
bool url_encode_query(std::string_view words, char* out, size_t size);

}  // namespace vellum

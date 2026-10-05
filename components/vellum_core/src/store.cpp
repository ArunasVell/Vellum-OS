#include "vellum/store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vellum/textfile.h"
#include "vellum/upload_page.h"

namespace vellum::store {
namespace {

// Project Gutenberg's OPDS 1 catalogue. Its search takes field prefixes:
// "l." a language, "s." a subject. 25 books a page, with a "next" link.
constexpr const char* kGutenbergSearch = "https://www.gutenberg.org/ebooks/search.opds/";

constexpr List kGutenbergLists[] = {
    {0, "https://www.gutenberg.org/ebooks/search.opds/?sort_order=downloads"},
    {1, "https://www.gutenberg.org/ebooks/search.opds/?sort_order=release_date"},
    {2, "https://www.gutenberg.org/ebooks/search.opds/?query=l.lt&sort_order=downloads"},
    {3, "https://www.gutenberg.org/ebooks/search.opds/?query=s.adventure&sort_order=downloads"},
    {4, "https://www.gutenberg.org/ebooks/search.opds/?query=s.detective&sort_order=downloads"},
    {5, "https://www.gutenberg.org/ebooks/search.opds/?query=s.science+fiction&sort_order=downloads"},
    {6, "https://www.gutenberg.org/ebooks/search.opds/?query=s.children&sort_order=downloads"},
    {7, "https://www.gutenberg.org/ebooks/search.opds/?query=s.poetry&sort_order=downloads"},
};

// Standard Ebooks keeps its full OPDS catalogue for paying patrons (it asks
// for a login); the newest releases are an open Atom feed with downloads.
constexpr List kStandardEbooksLists[] = {
    {8, "https://standardebooks.org/feeds/atom/new-releases"},
};

constexpr Source kSources[] = {
    {"Project Gutenberg", 0, kGutenbergLists, sizeof(kGutenbergLists) / sizeof(kGutenbergLists[0])},
    {"Standard Ebooks", 1, kStandardEbooksLists, sizeof(kStandardEbooksLists) / sizeof(kStandardEbooksLists[0])},
};

}  // namespace

int source_count() { return sizeof(kSources) / sizeof(kSources[0]); }

const Source& source(int i) { return kSources[i]; }

bool search_url(const char* words, char* out, size_t size) {
  char query[160];
  if (!url_encode_query(words, query, sizeof(query))) return false;
  const int n = std::snprintf(out, size, "%s?query=%s", kGutenbergSearch, query);
  return n >= 0 && static_cast<size_t>(n) < size;
}

NetResult fetch_feed(Network& net, const char* url, OpdsFeed& feed, OpdsParser& parser) {
  parser.begin(feed, url);
  const NetResult r = net.get(
      url,
      [](void* ctx, const uint8_t* data, uint32_t len, uint32_t) {
        static_cast<OpdsParser*>(ctx)->feed(reinterpret_cast<const char*>(data), len);
        return true;
      },
      &parser);
  if (r != NetResult::Ok) return r;
  return parser.finish() ? NetResult::Ok : NetResult::NoServer;
}

bool book_path(Storage& storage, const char* title, const char* author, char* out, size_t size) {
  // The main title: "Frankenstein; Or, The Modern Prometheus" is Frankenstein.
  std::string_view main(title);
  const size_t cut = main.find_first_of(":;");
  if (cut != std::string_view::npos && cut > 0) main = main.substr(0, cut);
  while (!main.empty() && main.back() == ' ') main.remove_suffix(1);
  if (main.empty()) main = "Book";
  char wanted[200];
  if (author != nullptr && author[0] != '\0') {
    std::snprintf(wanted, sizeof(wanted), "%.60s - %.*s.epub", author,
                  static_cast<int>(std::min<size_t>(main.size(), 120)), main.data());
  } else {
    std::snprintf(wanted, sizeof(wanted), "%.*s.epub", static_cast<int>(std::min<size_t>(main.size(), 120)),
                  main.data());
  }
  char name[128];
  if (!safe_book_name(wanted, name, sizeof(name))) return false;
  const size_t stem = std::strlen(name) - 5;  // before ".epub"
  for (int n = 1; n < 100; ++n) {
    int len;
    if (n == 1) {
      len = std::snprintf(out, size, "books/%s", name);
    } else {
      len = std::snprintf(out, size, "books/%.*s %d.epub", static_cast<int>(stem), name, n);
    }
    if (len < 0 || static_cast<size_t>(len) >= size) return false;
    File* f = storage.open(out);
    if (f == nullptr) return true;
    storage.close(f);
  }
  return false;
}

Saved download(Network& net, Storage& storage, const char* url, const char* path, ProgressFn progress, void* ctx) {
  struct Sink {
    Writer* writer;
    ProgressFn progress;
    void* ctx;
    uint32_t got = 0;
    int percent = -1;
    bool not_book = false;
    bool write_failed = false;
    char head[2] = {};
  } sink{storage.create(path), progress, ctx};
  if (sink.writer == nullptr) return Saved::CardFull;
  const NetResult r = net.get(
      url,
      [](void* c, const uint8_t* data, uint32_t len, uint32_t total) {
        auto& s = *static_cast<Sink*>(c);
        // An EPUB is a ZIP file: it starts "PK". Anything else (a web page
        // asking to sign in, say) is not kept.
        for (uint32_t i = 0; i < len && s.got + i < 2; ++i) s.head[s.got + i] = static_cast<char>(data[i]);
        if (s.got + len >= 2 && (s.head[0] != 'P' || s.head[1] != 'K')) {
          s.not_book = true;
          return false;
        }
        if (!s.writer->write(data, len)) {
          s.write_failed = true;
          return false;
        }
        s.got += len;
        if (total > 0 && s.progress != nullptr) {
          const int percent = static_cast<int>(uint64_t{s.got} * 100 / total);
          if (percent != s.percent) {
            s.percent = percent;
            s.progress(s.ctx, percent);
          }
        }
        return true;
      },
      &sink);
  const bool whole = r == NetResult::Ok && sink.got >= 2;
  const bool kept = storage.finish(sink.writer, whole);
  if (sink.write_failed) return Saved::CardFull;
  if (sink.not_book || (r == NetResult::Ok && sink.got < 2)) return Saved::NotBook;
  switch (r) {
    case NetResult::Ok:
      return kept ? Saved::Ok : Saved::CardFull;
    case NetResult::NoNetwork:
      return Saved::NoNetwork;
    case NetResult::NoWifi:
      return Saved::NoWifi;
    default:
      return Saved::NoServer;
  }
}

void format_size(Language lang, uint32_t bytes, char* out, size_t size) {
  if (bytes >= 1024 * 1024) {
    char number[16];
    format_tenths(lang, static_cast<int>((uint64_t{bytes} * 10 + 512 * 1024) / (1024 * 1024)), number, sizeof(number));
    std::snprintf(out, size, "%s MB", number);
  } else {
    std::snprintf(out, size, "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
  }
}

}  // namespace vellum::store

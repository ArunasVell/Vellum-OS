#include "vellum/text_book.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

#include "vellum/covers.h"
#include "vellum/text.h"
#include "vellum/xml.h"

namespace vellum {
namespace {

using charset::Encoding;

constexpr uint32_t kTextBytes = 3 * TextBook::kChapterBytes + TextBook::kSpare;
constexpr uint32_t kMaxHeadingLine = 120;                        // bytes of UTF-8; longer lines are text
constexpr uint32_t kHardSplit = TextBook::kChapterBytes - 4096;  // cut at any tag past this
constexpr char kTitleBreak = '\x1F';                             // between the lines of a captured title
constexpr uint32_t kPicturePathBytes = 16 * 1024;
// Converted books are made again when the converters change: part of their name.
constexpr uint32_t kConverterVersion = 1;
constexpr uint32_t kComicScratch = 4096;  // a comic chapter's paragraphs; the rest of text_ holds a picture

uint32_t fnv(std::string_view s, uint32_t h = 2166136261u) {
  for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  return h;
}

char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Compares file names as people do: "page2" before "page10", any case.
bool natural_less(std::string_view a, std::string_view b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    const bool da = a[i] >= '0' && a[i] <= '9', db = b[j] >= '0' && b[j] <= '9';
    if (da && db) {
      size_t ie = i, je = j;
      while (ie < a.size() && a[ie] == '0') ++ie;
      while (je < b.size() && b[je] == '0') ++je;
      size_t ae = ie, be = je;
      while (ae < a.size() && a[ae] >= '0' && a[ae] <= '9') ++ae;
      while (be < b.size() && b[be] >= '0' && b[be] <= '9') ++be;
      if (ae - ie != be - je) return ae - ie < be - je;
      const int c = a.substr(ie, ae - ie).compare(b.substr(je, be - je));
      if (c != 0) return c < 0;
      i = ae;
      j = be;
      continue;
    }
    const char ca = lower_ascii(a[i]), cb = lower_ascii(b[j]);
    if (ca != cb) return ca < cb;
    ++i;
    ++j;
  }
  return a.size() - i < b.size() - j;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

bool has_text(std::string_view s) {
  for (char c : s) {
    if (!is_space(c)) return true;
  }
  return false;
}

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Whether s ends in `ext` (lowercase), in any case.
bool ends_with(std::string_view s, std::string_view ext) {
  if (s.size() < ext.size()) return false;
  s = s.substr(s.size() - ext.size());
  for (size_t i = 0; i < ext.size(); ++i) {
    if (ascii_lower(s[i]) != ext[i]) return false;
  }
  return true;
}

// Copies s into out (NUL-terminated), cut at a character boundary to fit.
void copy_utf8(std::string_view s, char* out, size_t size) {
  size_t n = std::min<size_t>(s.size(), size - 1);
  if (n < s.size()) {
    while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
  }
  if (n > 0) std::memcpy(out, s.data(), n);
  out[n] = '\0';
}

// Converts raw text in `e` to UTF-8 in out: tags dropped, entities decoded,
// whitespace collapsed, kTitleBreak turned into " · ". `utf8` is scratch
// space of kCleanBytes, kept off the stack.
constexpr uint32_t kCleanBytes = 3 * 512;
void clean_text(std::string_view raw, Encoding e, bool xml_text, char* out, size_t size, char* utf8) {
  raw = raw.substr(0, kCleanBytes / 3);
  const uint32_t n = charset::to_utf8(e, reinterpret_cast<const uint8_t*>(raw.data()),
                                      static_cast<uint32_t>(raw.size()), utf8, kCleanBytes);
  const std::string_view s(utf8, n);
  size_t w = 0;
  bool space = false;
  bool brk = false;
  for (size_t i = 0; i < s.size() && w + 8 < size;) {
    const char c = s[i];
    if (xml_text && c == '<') {
      const size_t gt = s.find('>', i);
      i = gt == std::string_view::npos ? s.size() : gt + 1;
      continue;
    }
    if (c == kTitleBreak) {
      brk = w > 0;
      ++i;
      continue;
    }
    if (is_space(c)) {
      space = w > 0;
      ++i;
      continue;
    }
    if (brk) {
      std::memcpy(out + w, " \xC2\xB7 ", 4);  // " · "
      w += 4;
    } else if (space) {
      out[w++] = ' ';
    }
    brk = space = false;
    size_t len = 0;
    if (xml_text && c == '&' && xml::decode_entity(s, i, out + w, len)) {
      w += len;
    } else {
      out[w++] = c;
      ++i;
    }
  }
  // Cut short: drop a partly copied character.
  size_t lead = w;
  while (lead > 0 && (static_cast<uint8_t>(out[lead - 1]) & 0xC0) == 0x80) --lead;
  if (lead > 0) {
    const auto c = static_cast<uint8_t>(out[lead - 1]);
    const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (w - (lead - 1) < need) w = lead - 1;
  }
  out[w] = '\0';
}

// The first words of a line, cut short with an ellipsis: a contents title
// for a part of a book that has no headings.
void first_words(std::string_view s, char* out, size_t size) {
  constexpr size_t kWords = 40;
  s = trim(s);
  if (s.size() <= kWords) {
    copy_utf8(s, out, size);
    return;
  }
  size_t cut = s.rfind(' ', kWords);
  if (cut == std::string_view::npos || cut < kWords / 2) {
    cut = kWords;
    while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;
  }
  while (cut > 0 && (s[cut - 1] == ',' || s[cut - 1] == ';' || s[cut - 1] == ':' || s[cut - 1] == ' ')) --cut;
  char buf[64];
  copy_utf8(s.substr(0, cut), buf, sizeof(buf));
  std::snprintf(out, size, "%s\xE2\x80\xA6", buf);  // …
}

// ---- Headings in plain text ------------------------------------------------

// Copies a word into out in lower case (ASCII and the Lithuanian capitals),
// without the punctuation around it.
std::string_view fold(std::string_view w, char* out, size_t cap) {
  while (!w.empty() && (w.front() == '(' || w.front() == '[')) w.remove_prefix(1);
  while (!w.empty() && std::strchr(".:,;)]!", w.back()) != nullptr) w.remove_suffix(1);
  size_t n = 0;
  for (size_t i = 0; i < w.size() && n + 2 < cap; ++i) {
    const auto c = static_cast<uint8_t>(w[i]);
    if ((c == 0xC4 || c == 0xC5) && i + 1 < w.size()) {
      const auto d = static_cast<uint8_t>(w[i + 1]);
      // Ą Č Ę Ė Į and Š Ū Ų Ž: the small letter is the next code point.
      const bool upper = c == 0xC4 ? (d == 0x84 || d == 0x8C || d == 0x98 || d == 0x96 || d == 0xAE)
                                   : (d == 0xA0 || d == 0xAA || d == 0xB2 || d == 0xBD);
      out[n++] = static_cast<char>(c);
      out[n++] = static_cast<char>(upper ? d + 1 : d);
      ++i;
      continue;
    }
    out[n++] = ascii_lower(static_cast<char>(c));
  }
  return {out, n};
}

bool in_list(std::string_view word, std::initializer_list<const char*> list) {
  for (const char* item : list) {
    if (word == item) return true;
  }
  return false;
}

bool is_roman(std::string_view w) {
  if (w.empty() || w.size() > 7) return false;
  for (char c : w) {
    if (std::strchr("ivxlcdm", c) == nullptr) return false;
  }
  return true;
}

bool all_upper(std::string_view w) {
  for (char c : w) {
    if (c >= 'a' && c <= 'z') return false;
  }
  return true;
}

// A chapter number: digits ("12", "1-as"), a Roman numeral or a number word.
bool is_number(std::string_view w) {
  size_t digits = 0;
  while (digits < w.size() && w[digits] >= '0' && w[digits] <= '9') ++digits;
  if (digits > 0 && digits <= 4 && (digits == w.size() || w[digits] == '-')) return true;
  if (is_roman(w)) return true;
  if (in_list(w, {"one",       "two",      "three",    "four",   "five",     "six",      "seven",   "eight",
                  "nine",      "ten",      "eleven",   "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
                  "seventeen", "eighteen", "nineteen", "twenty", "first",    "second",   "third",   "fourth",
                  "fifth",     "sixth",    "seventh",  "eighth", "ninth",    "tenth",    "last",    "final"})) {
    return true;
  }
  // Lithuanian ordinals in any ending: pirmas, pirmasis, antroji...
  for (const char* stem :
       {"pirm", "antr", "treč", "ketvirt", "penkt", "šešt", "septint", "aštunt", "devint", "dešimt", "paskutin"}) {
    if (w.starts_with(stem)) return true;
  }
  return false;
}

// Words that head a numbered chapter, before or after its number.
bool is_chapter_word(std::string_view w) {
  return in_list(w, {"chapter", "part", "book", "section", "volume", "skyrius", "dalis", "knyga", "tomas", "chapitre",
                     "kapitel", "capítulo", "capitolo"});
}

// Headings that stand alone.
bool is_alone_word(std::string_view w) {
  return in_list(w, {"prologue", "epilogue", "preface", "foreword", "introduction", "afterword", "prologas", "epilogas",
                     "įžanga", "pratarmė", "įvadas", "pratarmės"});
}

// Whether a word ends in punctuation that separates a number from a title.
bool ends_label(std::string_view raw) {
  return !raw.empty() && (raw.back() == '.' || raw.back() == ':' || raw.back() == ')');
}

// A line that may follow a heading as its title: short, not a sentence.
bool is_title_line(std::string_view t) {
  if (t.empty() || t.size() > 80 || TextBook::is_heading(t)) return false;
  if (std::strchr(".!?,;:", t.back()) != nullptr) return false;
  const auto c = static_cast<uint8_t>(t.front());
  if ((c >= 'a' && c <= 'z') || c == '"' || c == '-' || c == '\'' || c == 0xE2) return false;  // E2: dashes, quotes
  return true;
}

// ---- Reading a file a window at a time --------------------------------------

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
    if (w + want >= size) break;
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

int base64_value(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

}  // namespace

// ---- Opening ----------------------------------------------------------------

TextBook::Format TextBook::format_of(const char* path) {
  const std::string_view p(path);
  if (ends_with(p, ".txt")) return Format::Text;
  if (ends_with(p, ".fb2")) return Format::Fb2;
  if (ends_with(p, ".fb2.zip")) return Format::Fb2Zip;
  if (ends_with(p, ".html") || ends_with(p, ".htm")) return Format::Html;
  if (ends_with(p, ".cbz")) return Format::Comic;
  switch (convert::kind_of(path)) {
    case convert::Kind::Mobi:
      return Format::Mobi;
    case convert::Kind::Pdf:
      return Format::Pdf;
    case convert::Kind::Docx:
      return Format::Docx;
    case convert::Kind::Markdown:
      return Format::Markdown;
    case convert::Kind::Rtf:
      return Format::Rtf;
    case convert::Kind::None:
      break;
  }
  return Format::None;
}

TextBook::TextBook()
    : raw_(new(std::nothrow) uint8_t[kChapterBytes]),
      text_(new(std::nothrow) char[kTextBytes + 1]),
      paragraphs_(new(std::nothrow) const char*[kMaxParagraphs]),
      parts_(new(std::nothrow) Part[kMaxChapters]),
      toc_(new(std::nothrow) TocEntry[kMaxToc]),
      toc_section_(new(std::nothrow) uint16_t[kMaxToc]),
      runs_(new(std::nothrow) StyleRun[kMaxRuns]),
      pictures_(new(std::nothrow) Picture[kMaxPictures]),
      picture_paths_(new(std::nothrow) char[kPicturePathBytes]),
      comic_(new(std::nothrow) uint16_t[Zip::kMaxEntries]) {}

TextBook::~TextBook() {
  close();
  delete[] raw_;
  delete[] text_;
  delete[] paragraphs_;
  delete[] parts_;
  delete[] toc_;
  delete[] toc_section_;
  delete[] runs_;
  delete[] pictures_;
  delete[] picture_paths_;
  delete[] comic_;
}

void TextBook::close() {
  if (file_ != nullptr) storage_->close(file_);
  file_ = nullptr;
  format_ = Format::None;
  encoding_ = Encoding::Utf8;
  wrapped_ = false;
  annotation_ = -1;
  cover_at_ = cover_len_ = 0;
  paragraph_count_ = 0;
  part_count_ = 0;
  part_open_ = false;
  loaded_ = -1;
  toc_count_ = 0;
  heading_[0][0] = heading_[1][0] = '\0';
  info_ = {};
  run_count_ = picture_count_ = picture_paths_used_ = 0;
  base_[0] = cover_[0] = '\0';
  comic_count_ = 0;
  contents_from_marks_ = false;
  zip_.close();
}

bool TextBook::open(Storage& storage, const char* path, bool convert) {
  close();
  waiting_ = false;
  if (raw_ == nullptr || text_ == nullptr || paragraphs_ == nullptr || parts_ == nullptr || toc_ == nullptr ||
      toc_section_ == nullptr) {
    return false;
  }
  const Format format = format_of(path);
  if (format == Format::None) return false;
  storage_ = &storage;
  key_ = fnv(path);
  if (format == Format::Comic) {
    if (!open_comic(path)) {
      close();
      return false;
    }
    format_ = format;
    if (info_.title[0] == '\0') title_from_name(path);
    return true;
  }
  const convert::Kind kind = convert::kind_of(path);
  if (format == Format::Fb2Zip) {
    if (!open_zipped(path)) return false;
  } else if (kind != convert::Kind::None) {
    if (!open_converted(path, kind, convert)) {
      close();
      return false;
    }
  } else {
    file_ = storage.open(path);
    if (file_ == nullptr) return false;
    // Pictures a web page names are found from its folder.
    const char* slash = std::strrchr(path, '/');
    std::snprintf(base_, sizeof(base_), "%.*s", slash == nullptr ? 0 : static_cast<int>(slash - path), path);
  }
  format_ = format;
  bool ok = false;
  switch (format) {
    case Format::Text:
      ok = scan_text();
      break;
    case Format::Fb2:
    case Format::Fb2Zip:
      ok = scan_fb2();
      break;
    case Format::Html:
    case Format::Mobi:
    case Format::Pdf:
    case Format::Docx:
    case Format::Markdown:
    case Format::Rtf:
      ok = scan_html();
      break;
    case Format::Comic:
    case Format::None:
      break;
  }
  if (!ok || part_count_ == 0) {
    close();
    return false;
  }
  if (info_.title[0] == '\0' || (format == Format::Text && info_.author[0] == '\0')) title_from_name(path);
  return true;
}

void TextBook::remove_copies(Storage& storage, const char* path, uint32_t size) {
  uint32_t h = fnv(path);
  for (int i = 0; i < 4; ++i) h = (h ^ ((size >> (8 * i)) & 0xFF)) * 16777619u;
  char name[96];
  std::snprintf(name, sizeof(name), "%s/%08lx.fb2", kUnzipFolder, static_cast<unsigned long>(h));
  storage.remove(name);
  h = (h ^ kConverterVersion) * 16777619u;
  for (const char* ext : {".html", ".no", ".tmp"}) {
    std::snprintf(name, sizeof(name), "%s/%08lx%s", kConvertFolder, static_cast<unsigned long>(h), ext);
    storage.remove(name);
  }
  // The pictures, "<hash>-<name>": names are collected before deleting, as
  // the storage may reuse its listing buffer, a few at a time.
  struct Found {
    char prefix[16];
    char names[16][64];
    int count;
  } found;
  std::snprintf(found.prefix, sizeof(found.prefix), "%08lx-", static_cast<unsigned long>(h));
  for (int round = 0; round < 64; ++round) {
    found.count = 0;
    storage.list(
        kConvertFolder,
        [](void* ctx, const char* entry, bool is_dir, uint32_t) {
          auto& f = *static_cast<Found*>(ctx);
          if (is_dir || f.count >= 16 || std::strncmp(entry, f.prefix, 9) != 0) return;
          if (std::strlen(entry) >= sizeof(f.names[0])) return;
          std::snprintf(f.names[f.count++], sizeof(f.names[0]), "%s", entry);
        },
        &found);
    bool removed = false;
    for (int k = 0; k < found.count; ++k) {
      std::snprintf(name, sizeof(name), "%s/%s", kConvertFolder, found.names[k]);
      removed = storage.remove(name) || removed;
    }
    if (found.count < 16 || !removed) break;
  }
}

bool TextBook::open_zipped(const char* path) {
  File* zipped = storage_->open(path);
  if (zipped == nullptr) return false;
  bool ok = false;
  if (zip_.open(zipped)) {
    int entry = -1;
    for (int i = 0; i < zip_.count() && entry < 0; ++i) {
      if (ends_with(zip_.name(i), ".fb2")) entry = i;
    }
    if (entry >= 0) {
      // One unpacked copy per book, named like its cover thumbnails.
      uint32_t h = 2166136261u;  // FNV-1a over the path and size
      for (const char* p = path; *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
      for (int i = 0; i < 4; ++i) h = (h ^ ((zipped->size() >> (8 * i)) & 0xFF)) * 16777619u;
      char unpacked[64];
      std::snprintf(unpacked, sizeof(unpacked), "%s/%08lx.fb2", kUnzipFolder, static_cast<unsigned long>(h));
      file_ = storage_->open(unpacked);
      if (file_ != nullptr && file_->size() != zip_.size(entry)) {
        storage_->close(file_);  // an unfinished or different copy
        file_ = nullptr;
      }
      if (file_ == nullptr) {
        Writer* out = storage_->create(unpacked);
        if (out != nullptr) {
          const bool done = zip_.extract_to(entry, *out, raw_);  // raw_ is the inflater's window
          if (storage_->finish(out, done) && done) file_ = storage_->open(unpacked);
        }
      }
      ok = file_ != nullptr;
    }
    zip_.close();
  }
  storage_->close(zipped);
  return ok;
}

bool TextBook::open_converted(const char* path, convert::Kind kind, bool convert) {
  File* in = storage_->open(path);
  if (in == nullptr) return false;
  // One converted copy per book, named by its path, size and the converters' version.
  uint32_t h = fnv(path);
  for (int i = 0; i < 4; ++i) h = (h ^ ((in->size() >> (8 * i)) & 0xFF)) * 16777619u;
  h = (h ^ kConverterVersion) * 16777619u;
  char page[64], note[64], pictures[64], scratch[64];
  std::snprintf(page, sizeof(page), "%s/%08lx.html", kConvertFolder, static_cast<unsigned long>(h));
  std::snprintf(note, sizeof(note), "%s/%08lx.no", kConvertFolder, static_cast<unsigned long>(h));
  std::snprintf(pictures, sizeof(pictures), "%s/%08lx-", kConvertFolder, static_cast<unsigned long>(h));
  std::snprintf(scratch, sizeof(scratch), "%s/%08lx.tmp", kConvertFolder, static_cast<unsigned long>(h));
  last_convert_ = convert::Result::Ok;
  file_ = storage_->open(page);
  if (file_ == nullptr) {
    // Tried before and could not be read: a note says why, so it is not tried at every start.
    if (File* no = storage_->open(note)) {
      uint8_t why = 0;
      no->read_at(0, &why, 1);
      storage_->close(no);
      last_convert_ = why >= '1' && why <= '4' ? static_cast<convert::Result>(why - '0') : convert::Result::Unreadable;
      storage_->close(in);
      return false;
    }
    if (!convert) {
      waiting_ = true;
      storage_->close(in);
      return false;
    }
    if (convert_fn_ != nullptr) convert_fn_(convert_ctx_, path);
    auto* work = new (std::nothrow) uint8_t[convert::kWorkBytes];
    convert::Result r = convert::Result::NoMemory;
    if (work != nullptr) {
      convert::Job job;
      job.storage = storage_;
      job.in = in;
      job.in_path = path;
      job.out_path = page;
      job.pictures = pictures;
      job.work = work;
      job.work_size = convert::kWorkBytes;
      job.zip = &zip_;
      job.scratch_path = scratch;
      job.progress = progress_fn_;
      job.progress_ctx = convert_ctx_;
      r = convert::run(kind, job);
      zip_.close();
    }
    delete[] work;
    last_convert_ = r;
    storage_->remove(scratch);
    if (r == convert::Result::Ok) {
      file_ = storage_->open(page);
    } else if (r != convert::Result::NoMemory && r != convert::Result::Cancelled) {
      const char why = static_cast<char>('0' + static_cast<int>(r));
      storage_->write(note, &why, 1);
    }
  }
  storage_->close(in);
  std::snprintf(base_, sizeof(base_), "%s", kConvertFolder);
  return file_ != nullptr;
}

bool TextBook::open_comic(const char* path) {
  if (comic_ == nullptr || pictures_ == nullptr) return false;
  file_ = storage_->open(path);
  if (file_ == nullptr || !zip_.open(file_)) return false;
  int about = -1;  // ComicInfo.xml
  for (int i = 0; i < zip_.count() && comic_count_ < Zip::kMaxEntries; ++i) {
    const std::string_view name = zip_.name(i);
    const size_t slash = name.rfind('/');
    const std::string_view base = slash == std::string_view::npos ? name : name.substr(slash + 1);
    if (base.empty() || base[0] == '.' || name.starts_with("__MACOSX")) continue;
    if (ends_with(base, ".jpg") || ends_with(base, ".jpeg") || ends_with(base, ".png")) {
      comic_[comic_count_++] = static_cast<uint16_t>(i);
    } else if (ends_with(base, "comicinfo.xml")) {
      about = i;
    }
  }
  if (comic_count_ == 0) return false;
  std::sort(comic_, comic_ + comic_count_,
            [&](uint16_t a, uint16_t b) { return natural_less(zip_.name(a), zip_.name(b)); });

  // Title, writer and language from ComicInfo.xml.
  if (about >= 0 && zip_.size(about) < kChapterBytes) {
    const int32_t n = zip_.extract(about, raw_, kChapterBytes - 1);
    const std::string_view x(reinterpret_cast<const char*>(raw_), n > 0 ? static_cast<size_t>(n) : 0);
    char series[96] = "", number[16] = "";
    size_t pos = 0, start = 0, before = 0;
    xml::Tag tag;
    while (xml::next_tag(x, pos, tag, start)) {
      const std::string_view text = x.substr(before, start - before);
      before = pos;
      if (!tag.closing) continue;
      char* out = xml::name_is(tag.name, "title")         ? info_.title
                  : xml::name_is(tag.name, "writer")      ? info_.author
                  : xml::name_is(tag.name, "languageiso") ? info_.language
                  : xml::name_is(tag.name, "series")      ? series
                  : xml::name_is(tag.name, "number")      ? number
                                                          : nullptr;
      const size_t size = out == info_.title      ? sizeof(info_.title)
                          : out == info_.author   ? sizeof(info_.author)
                          : out == info_.language ? sizeof(info_.language)
                          : out == series         ? sizeof(series)
                                                  : sizeof(number);
      if (out != nullptr) clean_text(text, Encoding::Utf8, true, out, size, text_);
    }
    if (info_.title[0] == '\0' && series[0] != '\0') {
      std::snprintf(info_.title, sizeof(info_.title), "%s%s%s", series, number[0] != '\0' ? " " : "", number);
    }
  }

  // Chapters: a folder each, cut every kComicPages pictures.
  std::string_view dir_before;
  int in_part = 0;
  bool folders = false;
  for (int k = 0; k < comic_count_; ++k) {
    const std::string_view name = zip_.name(comic_[k]);
    const size_t slash = name.rfind('/');
    folders |= slash != std::string_view::npos;
  }
  for (int k = 0; k < comic_count_; ++k) {
    const std::string_view name = zip_.name(comic_[k]);
    const size_t slash = name.rfind('/');
    const std::string_view dir = slash == std::string_view::npos ? std::string_view{} : name.substr(0, slash);
    const bool new_dir = k == 0 || dir != dir_before;
    if (new_dir || in_part == kComicPages) {
      if (part_count_ >= kMaxChapters) break;
      parts_[part_count_++] = {static_cast<uint32_t>(k), 0};
      in_part = 0;
      char title[96];
      const size_t last = dir.rfind('/');
      if (folders && new_dir && !dir.empty()) {
        copy_utf8(last == std::string_view::npos ? dir : dir.substr(last + 1), title, sizeof(title));
      } else {
        const int to = std::min(k + kComicPages, comic_count_);
        std::snprintf(title, sizeof(title), "%d\xE2\x80\x93%d", k + 1, to);  // 1–20
      }
      if (!folders || new_dir) add_toc(title, 0, 0);
    }
    ++parts_[part_count_ - 1].length;
    ++in_part;
    dir_before = dir;
  }
  return part_count_ > 0;
}

void TextBook::load_comic(int i) {
  const Part& p = parts_[i];
  paragraph_count_ = picture_count_ = run_count_ = 0;
  char* w = text_;
  for (uint32_t k = p.offset; k < p.offset + p.length && picture_count_ < kMaxPictures; ++k) {
    paragraphs_[paragraph_count_] = w;
    *w++ = kPictureMark;
    *w++ = '\0';
    pictures_[picture_count_++] = {paragraph_count_, comic_[k], 0, 0};
    ++paragraph_count_;
  }
  measure_pictures();
}

const char* TextBook::picture_file(int32_t entry) const {
  if (entry < 0 || static_cast<uint32_t>(entry) >= kMaxPictures || picture_paths_ == nullptr) return nullptr;
  return picture_paths_ + picture_at_[entry];
}

int TextBook::picture_entry(void* ctx, std::string_view src) {
  auto& b = *static_cast<TextBook*>(ctx);
  // Only pictures on the card: not on the web, not inline data.
  if (b.picture_paths_ == nullptr || src.empty() || src.find("://") != std::string_view::npos ||
      src.starts_with("data:")) {
    return -1;
  }
  if (const size_t cut = src.find_first_of("?#"); cut != std::string_view::npos) src = src.substr(0, cut);
  // Where it is: the page's folder, then the source, its %20s decoded, its ".." followed.
  char path[256];
  size_t n = 0;
  auto put = [&](char c) {
    if (n + 1 < sizeof(path)) path[n++] = c;
  };
  if (src[0] == '/') {
    src.remove_prefix(1);
  } else {
    for (const char* c = b.base_; *c; ++c) put(*c);
    if (n > 0) put('/');
  }
  for (size_t k = 0; k < src.size(); ++k) {
    if (src[k] == '%' && k + 2 < src.size()) {
      auto hex = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
      };
      const int hi = hex(src[k + 1]), lo = hex(src[k + 2]);
      if (hi >= 0 && lo >= 0) {
        put(static_cast<char>(hi << 4 | lo));
        k += 2;
        continue;
      }
    }
    put(src[k]);
  }
  path[n] = '\0';
  // Segments: "." dropped, ".." takes the one before away.
  char clean[256];
  size_t w = 0;
  for (size_t k = 0; k <= n;) {
    size_t e = k;
    while (e < n && path[e] != '/') ++e;
    const std::string_view seg(path + k, e - k);
    if (seg == "..") {
      while (w > 0 && clean[w - 1] != '/') --w;
      if (w > 0) --w;
    } else if (!seg.empty() && seg != ".") {
      if (w > 0) clean[w++] = '/';
      std::memcpy(clean + w, seg.data(), seg.size());
      w += seg.size();
    }
    k = e + 1;
  }
  clean[w] = '\0';
  if (w == 0 || b.picture_paths_used_ + w + 1 > kPicturePathBytes) return -1;
  // The index of the picture being added.
  const uint32_t entry = b.picture_count_now_;
  if (entry >= kMaxPictures) return -1;
  b.picture_at_[entry] = b.picture_paths_used_;
  std::memcpy(b.picture_paths_ + b.picture_paths_used_, clean, w + 1);
  b.picture_paths_used_ += static_cast<uint32_t>(w + 1);
  ++b.picture_count_now_;
  return static_cast<int>(entry);
}

void TextBook::measure_pictures() {
  for (uint32_t k = 0; k < picture_count_; ++k) {
    Picture& p = pictures_[k];
    if (p.entry < 0) continue;
    int w = 0, h = 0, found = -1;
    if (format_ == Format::Comic) {
      for (uint32_t want : {uint32_t{4096}, uint32_t{65536}}) {
        const int32_t n = zip_.extract_head(p.entry, raw_, want);
        if (n <= 0) break;
        found = covers::picture_size(raw_, static_cast<uint32_t>(n), w, h);
        if (found >= 0 || static_cast<uint32_t>(n) < want) break;
      }
    } else if (const char* path = picture_file(p.entry)) {
      File* f = storage_->open(path);
      if (f == nullptr) continue;
      for (uint32_t want : {uint32_t{4096}, uint32_t{65536}}) {
        const uint32_t n = std::min<uint32_t>(want, f->size());
        if (n == 0 || f->read_at(0, raw_, n) != static_cast<int32_t>(n)) break;
        found = covers::picture_size(raw_, n, w, h);
        if (found >= 0 || n < want) break;
      }
      storage_->close(f);
    }
    if (found == 1) {
      p.w = static_cast<uint16_t>(w);
      p.h = static_cast<uint16_t>(h);
    }
  }
}

ChapterMarkup TextBook::markup() const {
  if (loaded_ < 0) return {};
  return {runs_, run_count_, nullptr, 0, pictures_, picture_count_};
}

bool TextBook::draw_picture(Framebuffer& fb, const Picture& p, Rect r) {
  if (p.entry < 0 || loaded_ < 0 || storage_ == nullptr || file_ == nullptr) return false;
  // Kept on the card once drawn; raw_ is free while pages are drawn.
  char keep[96];
  if (format_ == Format::Comic) {
    covers::picture_path(key_, p.entry, r.w, r.h, keep, sizeof(keep));
    if (covers::draw_kept(*storage_, keep, fb, r, raw_, kChapterBytes)) return true;
    const uint32_t at = zip_.stored_at(p.entry);
    if (at > 0) return covers::draw_file(storage_, keep, fb, r, *file_, at, zip_.size(p.entry), raw_, kChapterBytes);
    auto* data = reinterpret_cast<uint8_t*>(text_) + kComicScratch;
    const int32_t n = zip_.extract(p.entry, data, kTextBytes - kComicScratch);
    return n > 0 && covers::draw_new(storage_, keep, fb, r, data, static_cast<uint32_t>(n), raw_, kChapterBytes);
  }
  const char* path = picture_file(p.entry);
  if (path == nullptr) return false;
  covers::picture_path(key_, static_cast<int>(fnv(path) & 0x7FFFFFFF), r.w, r.h, keep, sizeof(keep));
  if (covers::draw_kept(*storage_, keep, fb, r, raw_, kChapterBytes)) return true;
  File* f = storage_->open(path);
  if (f == nullptr) return false;
  const bool ok = covers::draw_file(storage_, keep, fb, r, *f, 0, f->size(), raw_, kChapterBytes);
  storage_->close(f);
  return ok;
}

bool TextBook::make_cover(const char* cover_path) {
  if (file_ == nullptr || storage_ == nullptr) return false;
  if (format_ == Format::Comic) {
    const int e = comic_[0];
    const uint32_t at = zip_.stored_at(e);
    if (at > 0) return covers::make_file(*storage_, cover_path, *file_, at, zip_.size(e));
    loaded_ = -1;  // unpacked into the chapter's buffer
    auto* data = reinterpret_cast<uint8_t*>(text_);
    const int32_t n = zip_.extract(e, data, kTextBytes);
    return n > 0 && covers::make(*storage_, cover_path, data, static_cast<uint32_t>(n));
  }
  if (cover_[0] != '\0') {
    File* f = storage_->open(cover_);
    if (f == nullptr) return false;
    const bool ok = covers::make_file(*storage_, cover_path, *f, 0, f->size());
    storage_->close(f);
    return ok;
  }
  const uint8_t* data = nullptr;
  const int32_t n = read_cover(&data);
  return n > 0 && covers::make(*storage_, cover_path, data, static_cast<uint32_t>(n));
}

bool TextBook::begin_part(uint32_t offset) {
  if (part_count_ >= kMaxChapters) return false;  // the last part goes on
  end_part(offset);
  parts_[part_count_++] = {offset, 0};
  part_open_ = true;
  return true;
}

void TextBook::end_part(uint32_t offset) {
  if (!part_open_) return;
  Part& p = parts_[part_count_ - 1];
  p.length = offset > p.offset ? offset - p.offset : 0;
  part_open_ = false;
}

void TextBook::add_toc(std::string_view title, int depth, uint32_t paragraph, uint16_t section) {
  if (toc_count_ >= kMaxToc || part_count_ == 0 || title.empty()) return;
  TocEntry& e = toc_[toc_count_];
  e = {};
  e.chapter = static_cast<uint16_t>(part_count_ - 1);
  e.depth = static_cast<uint8_t>(std::clamp(depth, 0, 255));
  e.paragraph = paragraph;
  copy_utf8(title, e.title, sizeof(e.title));
  toc_section_[toc_count_] = section;
  ++toc_count_;
}

void TextBook::title_from_name(const char* path) { title_from_name(path, info_); }

void TextBook::title_from_name(const char* path, EpubInfo& info, bool split) {
  const char* slash = std::strrchr(path, '/');
  std::string_view name(slash != nullptr ? slash + 1 : path);
  for (const char* ext : {".fb2.zip", ".fb2", ".txt", ".html", ".htm", ".mobi", ".azw3", ".azw", ".prc", ".pdb", ".pdf",
                          ".docx", ".markdown", ".md", ".rtf", ".cbz"}) {
    if (ends_with(name, ext)) {
      name.remove_suffix(std::strlen(ext));
      break;
    }
  }
  char clean[160];
  size_t n = 0;
  for (char c : name) {
    if (n + 1 < sizeof(clean)) clean[n++] = c == '_' ? ' ' : c;
  }
  std::string_view s(clean, n);
  // "Author - Title", as many downloaded books are named.
  const size_t dash = s.find(" - ");
  if (split && info.author[0] == '\0' && info.title[0] == '\0' && dash != std::string_view::npos && dash > 0) {
    copy_utf8(trim(s.substr(0, dash)), info.author, sizeof(info.author));
    s = s.substr(dash + 3);
  }
  if (info.title[0] == '\0') copy_utf8(trim(s), info.title, sizeof(info.title));
}

void TextBook::guess_language(const char* text, uint32_t len) {
  uint32_t letters = 0, lithuanian = 0, cyrillic = 0, the = 0;
  for (uint32_t i = 0; i < len; ++i) {
    const auto c = static_cast<uint8_t>(text[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ++letters;
    if (c == 0xD0 || c == 0xD1) ++cyrillic;
    if (i + 1 < len && (c == 0xC4 || c == 0xC5)) {
      const auto d = static_cast<uint8_t>(text[i + 1]);
      // ą č ę ė į š ū ų ž and their capitals
      if ((c == 0xC4 && (d == 0x84 || d == 0x85 || d == 0x8C || d == 0x8D || d == 0x98 || d == 0x99 || d == 0x96 ||
                         d == 0x97 || d == 0xAE || d == 0xAF)) ||
          (c == 0xC5 &&
           (d == 0xA0 || d == 0xA1 || d == 0xAA || d == 0xAB || d == 0xB2 || d == 0xB3 || d == 0xBD || d == 0xBE))) {
        ++lithuanian;
      }
    }
    if (i + 4 < len && (c == ' ' || c == '\n') && ascii_lower(text[i + 1]) == 't' && text[i + 2] == 'h' &&
        text[i + 3] == 'e' && text[i + 4] == ' ') {
      ++the;
    }
  }
  const char* lang = "";
  if (lithuanian * 40 > letters && lithuanian > 0) {
    lang = "lt";
  } else if (cyrillic > letters) {
    lang = "ru";
  } else if (the > 0 && the * 400 > letters) {
    lang = "en";
  }
  std::snprintf(info_.language, sizeof(info_.language), "%s", lang);
}

// ---- Plain text ---------------------------------------------------------------

bool TextBook::is_heading(std::string_view line) {
  line = trim(line);
  if (line.empty() || line.size() > kMaxHeadingLine) return false;
  std::string_view raw[3];
  int count = 0;
  for (size_t i = 0; i < line.size();) {
    size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
    if (count < 3) raw[count] = line.substr(i, j - i);
    ++count;
    i = j;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  }
  char b0[32], b1[32];
  const std::string_view w0 = fold(raw[0], b0, sizeof(b0));
  const std::string_view w1 = count > 1 ? fold(raw[1], b1, sizeof(b1)) : std::string_view{};
  // A title may follow the number when it is set off ("Chapter 1. Home") or
  // when the line does not read as a sentence.
  const bool titled = count == 2 || (count > 1 && ends_label(raw[1])) || std::strchr(".!?,;", line.back()) == nullptr;

  if (is_alone_word(w0)) return count == 1 || ends_label(raw[0]);
  // "Chapter 1", "CHAPTER IV. Title", "Part Two", "Skyrius 3"
  if (count >= 2 && is_chapter_word(w0) && is_number(w1)) return titled;
  // "1 skyrius", "Pirmas skyrius", "II DALIS"
  if (count >= 2 && is_number(w0) && in_list(w1, {"skyrius", "dalis", "knyga", "chapter", "part"})) return titled;
  // "XII", "IV.", "12"
  if (count == 1) {
    if (is_roman(w0)) return all_upper(raw[0]);
    size_t d = 0;
    while (d < w0.size() && w0[d] >= '0' && w0[d] <= '9') ++d;
    return d > 0 && d <= 3 && d == w0.size();
  }
  // "IV. The Return"
  if (raw[0].size() >= 2 && raw[0].back() == '.' && is_roman(w0) && all_upper(raw[0]) && line.size() <= 60) {
    const auto c = static_cast<uint8_t>(raw[1][0]);
    return (c >= 'A' && c <= 'Z') || c >= 0xC0;
  }
  return false;
}

uint32_t TextBook::split_text(const char* in, uint32_t len, char* out, const char** paragraphs, uint32_t max,
                              bool wrapped, uint32_t spare) {
  uint32_t count = 0;
  uint32_t w = 0;
  uint32_t para = 0;
  bool open = false;        // a paragraph is being written
  bool prev_blank = true;   // the line before was blank (or there was none)
  bool after_head = false;  // a title line may follow
  auto close = [&] {
    if (!open) return;
    out[w++] = '\0';
    if (count < max) paragraphs[count++] = out + para;
    open = false;
  };
  // Copies a trimmed line, runs of spaces made one and soft hyphens dropped.
  auto copy = [&](std::string_view t) {
    bool space = false;
    for (size_t i = 0; i < t.size(); ++i) {
      const char c = t[i];
      if (is_space(c)) {
        space = true;
        continue;
      }
      if (static_cast<uint8_t>(c) < 0x20) continue;
      if (c == '\xC2' && i + 1 < t.size() && t[i + 1] == '\xAD') {
        ++i;
        continue;
      }
      if (space) out[w++] = ' ';
      space = false;
      out[w++] = c;
    }
  };
  // The line starting at pos and where the next one starts.
  auto line_at = [&](uint32_t pos, uint32_t& next) {
    uint32_t e = pos;
    while (e < len && in[e] != '\n' && in[e] != '\r') ++e;
    next = e;
    if (next < len) next += (in[next] == '\r' && next + 1 < len && in[next + 1] == '\n') ? 2 : 1;
    return std::string_view(in + pos, e - pos);
  };

  for (uint32_t pos = 0; pos < len;) {
    uint32_t next;
    const std::string_view line = line_at(pos, next);
    const std::string_view t = trim(line);
    pos = next;
    if (t.empty()) {
      close();
      prev_blank = true;
      continue;
    }
    const bool heading = (!wrapped || prev_blank) && is_heading(t);
    bool title = false;
    if (!heading && after_head && is_title_line(t)) {
      uint32_t after;
      title = pos >= len || trim(line_at(pos, after)).empty();
    }
    if (heading || title) {
      close();
      para = w;
      open = true;
      if (spare > 0) {
        out[w++] = kHeadingMark;
        --spare;
      }
      copy(t);
      close();
      after_head = heading;
    } else {
      const bool indented = line[0] == ' ' || line[0] == '\t';
      if (!open || !wrapped || prev_blank || indented) {
        close();
        para = w;
        open = true;
      } else {
        out[w++] = ' ';
      }
      copy(t);
      after_head = false;
    }
    prev_blank = false;
  }
  close();
  return count;
}

bool TextBook::scan_text() {
  const uint32_t size = file_->size();
  const uint32_t first = std::min<uint32_t>(size, kChapterBytes);
  if (file_->read_at(0, raw_, first) != static_cast<int32_t>(first)) return false;
  uint32_t bom = 0;
  encoding_ = charset::detect(raw_, first, bom);
  if (encoding_ == Encoding::Utf8 && size > first) {
    // An English start can hide a code page further on: look there too.
    for (const uint32_t at : {size / 3, size / 3 * 2}) {
      const int32_t n = file_->read_at(at, raw_, 32768);
      if (n <= 0) continue;
      uint32_t skip = 0;  // into the middle of a character
      while (skip < static_cast<uint32_t>(n) && (raw_[skip] & 0xC0) == 0x80) ++skip;
      uint32_t ignore;
      const Encoding e = charset::detect(raw_ + skip, static_cast<uint32_t>(n) - skip, ignore);
      if (e != Encoding::Utf8 && e != Encoding::Utf16LE && e != Encoding::Utf16BE) {
        encoding_ = e;
        break;
      }
    }
  }
  const uint32_t u = charset::unit(encoding_);

  // How lines are used: a sample decides, as UTF-8.
  {
    const uint32_t n = std::min<uint32_t>(first, 65536);
    if (file_->read_at(0, raw_, n) != static_cast<int32_t>(n)) return false;
    const uint32_t len = charset::to_utf8(encoding_, raw_ + bom, n - bom, text_, kTextBytes);
    guess_language(text_, len);
    uint32_t lines = 0, long_lines = 0, blank = 0, indented = 0;
    for (uint32_t pos = 0; pos < len;) {
      uint32_t e = pos;
      uint32_t chars = 0;
      while (e < len && text_[e] != '\n') chars += (static_cast<uint8_t>(text_[e++]) & 0xC0) != 0x80;
      const std::string_view line(text_ + pos, e - pos);
      if (trim(line).empty()) {
        ++blank;
      } else {
        ++lines;
        long_lines += chars > 100;
        indented += line[0] == ' ' || line[0] == '\t';
      }
      pos = e + 1;
    }
    // Wrapped: short lines, with paragraphs set off by blank or indented lines.
    wrapped_ = lines >= 5 && long_lines * 10 < lines && (blank * 20 >= lines || indented * 20 >= lines);
  }

  auto unit_at = [&](uint32_t i) -> uint32_t {
    if (u == 1) return raw_[i];
    return encoding_ == Encoding::Utf16LE ? (raw_[i] | raw_[i + 1] << 8) : (raw_[i] << 8 | raw_[i + 1]);
  };
  // Finds the end of the line at pos in a window of n bytes. Returns false
  // when the window ends first (and is not the file's last).
  auto line_end = [&](uint32_t pos, uint32_t n, bool last, uint32_t& end, uint32_t& next) {
    uint32_t e = pos;
    while (e + u <= n && unit_at(e) != '\n' && unit_at(e) != '\r') e += u;
    if (e + u > n) {
      if (!last) return false;
      end = next = n;
      return true;
    }
    end = e;
    next = e + u;
    if (unit_at(e) == '\r' && next + u <= n && unit_at(next) == '\n') next += u;
    return true;
  };
  auto to_line = [&](uint32_t pos, uint32_t end, char* out, size_t cap) {
    const uint32_t n = charset::to_utf8(encoding_, raw_ + pos, end - pos, out, static_cast<uint32_t>(cap));
    return trim(std::string_view(out, n));
  };
  auto is_blank = [&](uint32_t pos, uint32_t end) {
    for (uint32_t i = pos; i + u <= end; i += u) {
      const uint32_t c = unit_at(i);
      if (c != ' ' && c != '\t' && c != '\f' && c != 0 && c != 0xFEFF) return false;
    }
    return true;
  };

  begin_part(bom);
  bool part_text = false;  // the current part has text
  bool prev_blank = true;
  bool headings = false;
  uint32_t w = bom;
  while (w < size) {
    const uint32_t n = std::min<uint32_t>(size - w, kChapterBytes);
    if (file_->read_at(w, raw_, n) != static_cast<int32_t>(n)) return false;
    const bool last = w + n >= size;
    uint32_t pos = 0;
    while (pos + u <= n) {
      uint32_t end, next;
      if (!line_end(pos, n, last, end, next)) {
        if (pos > 0) break;  // read again from this line
        // A line longer than the window: cut it, between characters.
        end = n;
        if (u == 1 && encoding_ == Encoding::Utf8) {
          uint32_t k = n - 1;
          while (k > 0 && (raw_[k] & 0xC0) == 0x80) --k;
          if (k > 0 && raw_[k] >= 0xC0) end = k;
        }
        next = end;
      }
      const uint32_t at = pos;
      pos = next;
      if (is_blank(at, end)) {
        prev_blank = true;
        continue;
      }
      const uint32_t off = w + at;
      const uint32_t len = end - at;
      const uint32_t part_len = off - parts_[part_count_ - 1].offset;
      char line_buf[3 * kMaxHeadingLine + 8];
      const bool short_line = len <= kMaxHeadingLine * u;
      const std::string_view line =
          to_line(at, short_line ? end : at + std::min(len, 60 * u), line_buf, sizeof(line_buf));
      const bool heading = short_line && (!wrapped_ || prev_blank) && is_heading(line);
      if (heading) {
        if (part_text) begin_part(off);
        headings = true;
        // A short line after the heading is its title: "Chapter I · The Beginning".
        char title[96];
        copy_utf8(line, title, sizeof(title));
        uint32_t p = next, e2, n2;
        int blanks = 0;
        while (blanks <= 2 && p + u <= n && line_end(p, n, last, e2, n2) && is_blank(p, e2)) {
          ++blanks;
          p = n2;
        }
        uint32_t e3, n3;
        if (blanks <= 2 && p + u <= n && line_end(p, n, last, e2, n2) && e2 - p <= 80 * u &&
            (n2 + u > n ? last : line_end(n2, n, last, e3, n3) && is_blank(n2, e3))) {
          char next_buf[3 * 80 + 8];
          const std::string_view t = to_line(p, e2, next_buf, sizeof(next_buf));
          if (is_title_line(t)) {
            const size_t used = std::strlen(title);
            const bool label = used > 0 && (title[used - 1] == '.' || title[used - 1] == ':');
            std::snprintf(title + used, sizeof(title) - used, "%s", label ? " " : " \xC2\xB7 ");
            const size_t used2 = std::strlen(title);
            copy_utf8(t, title + used2, sizeof(title) - used2);
          }
        }
        add_toc(title, 0, 0);
      } else if (part_text &&
                 ((part_len >= kSplitBytes && (!wrapped_ || prev_blank || unit_at(at) == ' ' || unit_at(at) == '\t')) ||
                  part_len + len > kChapterBytes)) {
        // A long stretch: a new part, named by its first words until the
        // book turns out to have headings.
        if (begin_part(off)) {
          char title[96];
          first_words(line, title, sizeof(title));
          add_toc(title, 0, 0, 1);
        }
      } else if (!part_text && part_count_ == 1) {
        char title[96];
        first_words(line, title, sizeof(title));
        add_toc(title, 0, 0);
      }
      part_text = true;
      prev_blank = false;
    }
    if (last) break;
    w += pos;
  }
  end_part(size);
  // Parts named by their first words are dropped from the contents when the
  // book has headings of its own.
  int kept = 0;
  for (int i = 0; i < toc_count_; ++i) {
    if (headings && toc_section_[i] == 1) continue;
    toc_[kept] = toc_[i];
    toc_section_[kept++] = 0;
  }
  toc_count_ = kept;
  return true;
}

// ---- FictionBook --------------------------------------------------------------

namespace {

// Reads the encoding named in an XML declaration at the start of s.
bool declared_encoding(std::string_view s, Encoding& out) {
  const size_t end = s.find("?>");
  if (!s.starts_with("<?xml") || end == std::string_view::npos) return false;
  s = s.substr(0, end);
  const size_t at = s.find("encoding");
  if (at == std::string_view::npos) return false;
  size_t i = s.find_first_of("\"'", at);
  if (i == std::string_view::npos) return false;
  const size_t close = s.find(s[i], i + 1);
  if (close == std::string_view::npos) return false;
  return charset::from_name(s.substr(i + 1, close - i - 1), out);
}

bool is_fb2_block(std::string_view n) {
  for (const char* b : {"p", "v", "stanza", "poem", "epigraph", "cite", "text-author", "annotation", "section", "table",
                        "tr", "td", "th", "body", "date"}) {
    if (xml::name_is(n, b)) return true;
  }
  return false;
}

}  // namespace

uint32_t TextBook::fb2_paragraphs(char* s, uint32_t len, const char** paragraphs, uint32_t max, SectionFn on_section,
                                  void* ctx) {
  const std::string_view v(s, len);
  uint32_t count = 0;
  size_t w = 0;           // write position; never passes the read position
  size_t para = 0;        // where the current paragraph starts
  bool space = false;     // whitespace seen since the last character
  int heading = 0;        // inside title or subtitle
  int skip = 0;           // inside binary or stylesheet
  uint32_t sections = 0;  // <section> tags so far
  size_t note = 0;        // where a footnote reference's text starts
  bool in_note = false;

  auto finish = [&] {
    if (w > para && count < max) {
      s[w++] = '\0';
      paragraphs[count++] = s + para;
    } else {
      w = para;
    }
    para = w;
    space = false;
  };

  size_t pos = 0;
  for (;;) {
    size_t start;
    xml::Tag tag;
    const size_t text_begin = pos;
    const bool more = xml::next_tag(v, pos, tag, start);
    const size_t text_end = more ? start : v.size();
    if (skip == 0) {
      for (size_t r = text_begin; r < text_end;) {
        const char c = s[r];
        if (c == '<') {  // a comment or CDATA block next_tag stepped over
          const bool comment = v.compare(r, 4, "<!--") == 0;
          const size_t close = v.find(comment ? "-->" : ">", r);
          if (close == std::string_view::npos || close >= text_end) break;
          r = close + (comment ? 3 : 1);
          continue;
        }
        if (is_space(c)) {
          space = w > para;
          ++r;
          continue;
        }
        char ch[4];
        size_t n = 1;
        if (c == '&') {
          size_t p = r;
          if (xml::decode_entity(v, p, ch, n)) {
            r = p;
          } else {
            ch[0] = c;
            n = 1;
            ++r;
          }
          if (n == 1 && is_space(ch[0])) {
            space = w > para;
            continue;
          }
          if (n == 0) continue;  // soft hyphen
        } else if (c == '\xC2' && r + 1 < text_end && s[r + 1] == '\xAD') {
          r += 2;  // soft hyphen
          continue;
        } else {
          ch[0] = c;
          ++r;
        }
        if (space) s[w++] = ' ';
        space = false;
        if (w == para && heading > 0) s[w++] = kHeadingMark;
        for (size_t k = 0; k < n; ++k) s[w++] = ch[k];
      }
    }
    if (!more) break;
    const std::string_view name = tag.name;
    if (xml::name_is(name, "binary") || xml::name_is(name, "stylesheet")) {
      if (!tag.self_closing) skip += tag.closing ? (skip > 0 ? -1 : 0) : 1;
    } else if (skip > 0) {
      // nothing inside skipped content
    } else if (xml::name_is(name, "title") || xml::name_is(name, "subtitle")) {
      finish();
      if (!tag.self_closing) heading += tag.closing ? (heading > 0 ? -1 : 0) : 1;
    } else if (xml::name_is(name, "empty-line")) {
      finish();
      if (count < max) paragraphs[count++] = "";  // a blank line
    } else if (xml::name_is(name, "a")) {
      // Footnote references, simplified: the note's number in brackets.
      if (!tag.closing && !tag.self_closing && xml::attr(tag.attrs, "type") == "note") {
        if (space && w > para) s[w++] = ' ';
        space = false;
        note = w;
        in_note = true;
      } else if (tag.closing && in_note) {
        in_note = false;
        // Room: the closing tag is four bytes, and writing is still before it.
        if (w > note && s[note] != '[' && s[note] != '(' && s[note] != '{') {
          std::memmove(s + note + 1, s + note, w - note);
          s[note] = '[';
          ++w;
          s[w++] = ']';
        }
      }
    } else if (is_fb2_block(name)) {
      finish();
      if (xml::name_is(name, "section") && !tag.closing) {
        ++sections;
        if (on_section != nullptr) on_section(ctx, sections, count);
      }
    }
  }
  finish();
  return count;
}

bool TextBook::scan_fb2() {
  const uint32_t size = file_->size();
  const uint32_t head = std::min<uint32_t>(size, 1024);
  if (head == 0 || file_->read_at(0, raw_, head) != static_cast<int32_t>(head)) return false;
  uint32_t bom = 0;
  encoding_ = charset::detect(raw_, head, bom);
  if (charset::unit(encoding_) != 1) return false;  // UTF-16 XML is not read
  Encoding declared;
  const std::string_view start_text(reinterpret_cast<const char*>(raw_ + bom), head - bom);
  if (declared_encoding(start_text, declared)) {
    encoding_ = declared;
  } else {
    encoding_ = Encoding::Utf8;  // the XML default
  }

  enum class Capture : uint8_t { None, BookTitle, First, Middle, Last, Nick, Lang, SectionTitle, NotesTitle };
  Capture capture = Capture::None;
  char cap[480];
  size_t cap_len = 0;
  char names[4][48] = {};  // first, middle, last, nickname
  bool in_desc = false, in_title_info = false, in_author = false, author_done = false;
  bool in_body = false, notes = false;
  bool part_text = false;
  bool fresh_section = false;  // the last tag opened a section
  int depth = 0;               // sections open in this body
  uint16_t section_n = 0;      // sections started in this part
  uint16_t title_section = 0;  // the section a title being read belongs to
  uint32_t title_section_at = 0;
  int title_depth = 0;
  uint32_t annotation_from = 0;
  char cover_id[64] = {};
  bool in_cover = false;

  auto new_part = [&](uint32_t offset) {
    if (!begin_part(offset)) return;
    part_text = false;
    section_n = 0;
  };
  auto part_len = [&](uint32_t offset) { return part_open_ ? offset - parts_[part_count_ - 1].offset : 0; };
  auto start_capture = [&](Capture c) {
    capture = c;
    cap_len = 0;
  };

  const bool ok = walk_tags(
      *file_, bom, raw_, kChapterBytes, [&](const xml::Tag& tag, std::string_view text, uint32_t start, uint32_t end) {
        if (capture != Capture::None) {
          const size_t n = std::min<size_t>(text.size(), sizeof(cap) - cap_len);
          std::memcpy(cap + cap_len, text.data(), n);
          cap_len += n;
        }
        if (in_body && !part_text && has_text(text)) part_text = true;
        const std::string_view name = tag.name;

        if (tag.closing) {
          auto take = [&](Capture c, char* out, size_t size) {
            if (capture != c) return false;
            clean_text({cap, cap_len}, encoding_, true, out, size, text_);
            capture = Capture::None;
            return true;
          };
          if (xml::name_is(name, "description")) {
            in_desc = false;
          } else if (xml::name_is(name, "title-info")) {
            in_title_info = false;
          } else if (xml::name_is(name, "author") && in_author) {
            in_author = false;
            author_done = true;
          } else if (take(Capture::BookTitle, info_.title, sizeof(info_.title)) ||
                     take(Capture::Lang, info_.language, sizeof(info_.language)) ||
                     take(Capture::First, names[0], sizeof(names[0])) ||
                     take(Capture::Middle, names[1], sizeof(names[1])) ||
                     take(Capture::Last, names[2], sizeof(names[2])) ||
                     take(Capture::Nick, names[3], sizeof(names[3]))) {
            // a field of the description
          } else if (xml::name_is(name, "annotation") && in_title_info && annotation_from > 0 && part_count_ == 0) {
            // The annotation is a chapter of its own, before the text.
            if (begin_part(annotation_from)) {
              end_part(start);
              annotation_ = 0;
            }
          } else if (xml::name_is(name, "coverpage")) {
            in_cover = false;
          } else if (xml::name_is(name, "title") &&
                     (capture == Capture::SectionTitle || capture == Capture::NotesTitle)) {
            char title[96];
            clean_text({cap, cap_len}, encoding_, true, title, sizeof(title), text_);
            if (capture == Capture::NotesTitle) {
              add_toc(title, 0, 0);
            } else {
              const bool at_start = title_section == 1 && parts_[part_count_ - 1].offset == title_section_at;
              add_toc(title, title_depth, at_start ? 0 : TocEntry::kUnknown, title_section);
            }
            capture = Capture::None;
          } else if (capture == Capture::SectionTitle || capture == Capture::NotesTitle) {
            if (xml::name_is(name, "p") && cap_len < sizeof(cap)) cap[cap_len++] = kTitleBreak;
          } else if (xml::name_is(name, "body") && in_body) {
            in_body = false;
            end_part(end);
            if (!part_text) {
              // Nothing to read in it: drop it, unless the contents point there.
              if (toc_count_ == 0 || toc_[toc_count_ - 1].chapter != part_count_ - 1) --part_count_;
            }
          } else if (xml::name_is(name, "binary") && cover_at_ > 0 && cover_len_ == 0 && start > cover_at_) {
            cover_len_ = start - cover_at_;
          } else if (in_body && is_fb2_block(name) && part_text && part_len(end) >= kSplitBytes) {
            new_part(end);  // a long stretch: go on in a new part
          }
          if (xml::name_is(name, "section") && in_body && depth > 0) --depth;
          return true;
        }

        // Opening tags.
        const bool fresh = fresh_section;
        fresh_section = false;
        if (in_body && part_text && part_len(start) >= kHardSplit) new_part(start);
        if (xml::name_is(name, "description")) {
          in_desc = true;
        } else if (in_desc && xml::name_is(name, "title-info")) {
          in_title_info = true;
        } else if (in_title_info && !tag.self_closing) {
          if (xml::name_is(name, "author") && !author_done) {
            in_author = true;
          } else if (in_author && xml::name_is(name, "first-name")) {
            start_capture(Capture::First);
          } else if (in_author && xml::name_is(name, "middle-name")) {
            start_capture(Capture::Middle);
          } else if (in_author && xml::name_is(name, "last-name")) {
            start_capture(Capture::Last);
          } else if (in_author && xml::name_is(name, "nickname")) {
            start_capture(Capture::Nick);
          } else if (xml::name_is(name, "book-title")) {
            start_capture(Capture::BookTitle);
          } else if (xml::name_is(name, "lang")) {
            start_capture(Capture::Lang);
          } else if (xml::name_is(name, "annotation")) {
            annotation_from = end;
          } else if (xml::name_is(name, "coverpage")) {
            in_cover = true;
          }
        }
        if (in_cover && xml::name_is(name, "image") && cover_id[0] == '\0') {
          std::string_view href = xml::attr(tag.attrs, "href");
          if (!href.empty() && href[0] == '#') href.remove_prefix(1);
          copy_utf8(href, cover_id, sizeof(cover_id));
        } else if (xml::name_is(name, "body") && !tag.self_closing) {
          in_body = true;
          notes = !xml::attr(tag.attrs, "name").empty();
          depth = 0;
          new_part(start);
        } else if (in_body && xml::name_is(name, "section") && !tag.self_closing) {
          ++depth;
          // Chapters start at the main text's top sections, and at deeper ones
          // when the part is already long.
          if (part_text && ((depth == 1 && !notes) || part_len(start) >= kSplitBytes)) new_part(start);
          ++section_n;
          fresh_section = true;
          title_section = section_n;
          title_section_at = start;
          title_depth = depth - 1;
        } else if (in_body && xml::name_is(name, "title") && !tag.self_closing) {
          if (fresh && !notes) {
            start_capture(Capture::SectionTitle);
          } else if (notes && depth == 0) {
            start_capture(Capture::NotesTitle);
          }
        } else if (xml::name_is(name, "binary") && cover_id[0] != '\0' && xml::attr(tag.attrs, "id") == cover_id) {
          cover_at_ = end;
        }
        return true;
      });
  if (!ok) return false;
  end_part(size);

  // The author as "First Middle Last", or a nickname.
  size_t n = 0;
  for (int k = 0; k < 3; ++k) {
    if (names[k][0] == '\0') continue;
    n += static_cast<size_t>(
        std::snprintf(info_.author + n, sizeof(info_.author) - n, "%s%s", n > 0 ? " " : "", names[k]));
    if (n >= sizeof(info_.author)) break;
  }
  if (info_.author[0] == '\0') std::snprintf(info_.author, sizeof(info_.author), "%s", names[3]);

  if (annotation_ == 0) {
    // The annotation's page opens with the book's title and author, and
    // the contents list it under the title.
    std::snprintf(heading_[0], sizeof(heading_[0]), "%c%s", kHeadingMark, info_.title);
    std::snprintf(heading_[1], sizeof(heading_[1]), "%c%s", kHeadingMark, info_.author);
    if (toc_count_ < kMaxToc) {
      std::memmove(toc_ + 1, toc_, sizeof(TocEntry) * toc_count_);
      std::memmove(toc_section_ + 1, toc_section_, sizeof(uint16_t) * toc_count_);
      toc_[0] = {};
      toc_[0].paragraph = 0;
      copy_utf8(info_.title, toc_[0].title, sizeof(toc_[0].title));
      toc_section_[0] = 0;
      ++toc_count_;
    }
  }
  return true;
}

// ---- Web pages ----------------------------------------------------------------

bool TextBook::scan_html() {
  const uint32_t size = file_->size();
  const uint32_t head = std::min<uint32_t>(size, 4096);
  if (head == 0 || file_->read_at(0, raw_, head) != static_cast<int32_t>(head)) return false;
  uint32_t bom = 0;
  encoding_ = charset::detect(raw_, head, bom);
  if (charset::unit(encoding_) != 1) return false;
  // <meta charset="..."> or <meta http-equiv... content="text/html; charset=...">
  char* lower = text_;  // free while scanning
  for (uint32_t i = 0; i < head; ++i) lower[i] = ascii_lower(static_cast<char>(raw_[i]));
  const std::string_view low(lower, head);
  if (const size_t at = low.find("charset="); at != std::string_view::npos) {
    size_t i = at + 8;
    while (i < low.size() && (low[i] == '"' || low[i] == '\'' || low[i] == ' ')) ++i;
    size_t e = i;
    while (e < low.size() && std::strchr("\"'; />", low[e]) == nullptr) ++e;
    Encoding named;
    if (charset::from_name(low.substr(i, e - i), named) && charset::unit(named) == 1) encoding_ = named;
  } else {
    Encoding declared;
    if (declared_encoding(low.substr(bom), declared)) encoding_ = declared;
  }

  char cap[480];
  size_t cap_len = 0;
  bool capturing = false;
  int level = 0;  // the heading being read
  bool in_body = false, part_text = false;
  int skip = 0;
  int min_depth = 255;
  begin_part(bom);

  const bool ok = walk_tags(
      *file_, bom, raw_, kChapterBytes, [&](const xml::Tag& tag, std::string_view text, uint32_t start, uint32_t end) {
        if (capturing) {
          const size_t n = std::min<size_t>(text.size(), sizeof(cap) - cap_len);
          std::memcpy(cap + cap_len, text.data(), n);
          cap_len += n;
        }
        if (in_body && skip == 0 && !part_text && has_text(text)) part_text = true;
        const std::string_view name = tag.name;
        if (in_body && skip == 0 && (xml::name_is(name, "img") || xml::name_is(name, "image"))) part_text = true;
        const bool heading =
            name.size() == 2 && (name[0] == 'h' || name[0] == 'H') && (name[1] == '1' || name[1] == '2');
        auto part_len = [&](uint32_t offset) { return part_open_ ? offset - parts_[part_count_ - 1].offset : 0; };

        if (xml::name_is(name, "script") || xml::name_is(name, "style")) {
          if (!tag.self_closing) skip += tag.closing ? (skip > 0 ? -1 : 0) : 1;
          return true;
        }
        if (skip > 0) return true;
        if (tag.closing) {
          if (capturing && xml::name_is(name, "title") && level == 0) {
            clean_text({cap, cap_len}, encoding_, true, info_.title, sizeof(info_.title), text_);
            capturing = false;
          } else if (capturing && heading && name[1] - '0' == level) {
            char title[96];
            clean_text({cap, cap_len}, encoding_, true, title, sizeof(title), text_);
            if (!contents_from_marks_) add_toc(title, level - 1, 0);
            if (title[0] != '\0') min_depth = std::min(min_depth, level - 1);
            capturing = false;
          } else if (in_body && part_text && part_len(end) >= kSplitBytes &&
                     (xml::name_is(name, "p") || xml::name_is(name, "div") || xml::name_is(name, "li") ||
                      xml::name_is(name, "blockquote") || xml::name_is(name, "pre") || xml::name_is(name, "table"))) {
            if (begin_part(end)) part_text = false;
          }
          return true;
        }
        if (xml::name_is(name, "html")) {
          copy_utf8(xml::attr(tag.attrs, "lang"), info_.language, sizeof(info_.language));
        } else if (xml::name_is(name, "title") && !in_body && !tag.self_closing && info_.title[0] == '\0') {
          capturing = true;
          level = 0;
          cap_len = 0;
        } else if (xml::name_is(name, "meta") && xml::attr(tag.attrs, "name") == "author") {
          clean_text(xml::attr(tag.attrs, "content"), encoding_, true, info_.author, sizeof(info_.author), text_);
        } else if (xml::name_is(name, "meta") && xml::attr(tag.attrs, "name") == "cover") {
          const std::string_view file = xml::attr(tag.attrs, "content");
          std::snprintf(cover_, sizeof(cover_), "%s%s%.*s", base_, base_[0] != '\0' ? "/" : "",
                        static_cast<int>(file.size()), file.data());
        } else if (xml::name_is(name, "meta") && xml::attr(tag.attrs, "name") == "vellum-contents") {
          contents_from_marks_ = xml::attr(tag.attrs, "content") == "marks";
        } else if (xml::name_is(name, convert::kChapterTag)) {
          // A converted book's own contents: a chapter starts here.
          in_body = true;
          if (part_text && begin_part(start)) part_text = false;
          char title[96];
          clean_text(xml::attr(tag.attrs, "title"), encoding_, true, title, sizeof(title), text_);
          int depth = 0;
          for (char c : xml::attr(tag.attrs, "depth")) {
            if (c >= '0' && c <= '9') depth = depth * 10 + (c - '0');
          }
          add_toc(title, std::min(depth, 8), 0);
          if (title[0] != '\0') min_depth = std::min(min_depth, depth);
        } else if (xml::name_is(name, "pagebreak")) {
          // <mbp:pagebreak/>: Kindle books start their chapters on a new page.
          if (in_body && part_text && begin_part(start)) part_text = false;
        } else if (xml::name_is(name, "body")) {
          in_body = true;
        } else if (heading && !tag.self_closing) {
          in_body = true;
          // Books with their own contents start chapters at its entries only.
          if (part_text && !contents_from_marks_ && begin_part(start)) part_text = false;
          capturing = true;
          level = name[1] - '0';
          cap_len = 0;
        } else if (in_body && part_text && part_len(start) >= kHardSplit) {
          if (begin_part(start)) part_text = false;
        } else if (xml::name_is(name, "p") || xml::name_is(name, "div")) {
          in_body = true;
        }
        return true;
      });
  if (!ok) return false;
  end_part(size);
  if (!part_text && part_count_ > 1) {
    // Nothing after the last break (Kindle books end with one): no empty chapter.
    --part_count_;
    for (int i = 0; i < toc_count_; ++i) toc_[i].chapter = std::min<uint16_t>(toc_[i].chapter, part_count_ - 1);
  }
  if (min_depth > 0 && min_depth < 255) {
    for (int i = 0; i < toc_count_; ++i) toc_[i].depth = static_cast<uint8_t>(toc_[i].depth - min_depth);
  }
  if (info_.language[0] == '\0') {
    // No lang attribute: guess from the start of the text.
    const uint32_t n = std::min<uint32_t>(parts_[0].length, 65536);
    if (file_->read_at(parts_[0].offset, raw_, n) == static_cast<int32_t>(n)) {
      const uint32_t len = charset::to_utf8(encoding_, raw_, n, text_, kTextBytes);
      guess_language(text_, len);
    }
  }
  return true;
}

// ---- Reading --------------------------------------------------------------------

uint32_t TextBook::chapter_weight(int i) const { return (i >= 0 && i < part_count_) ? parts_[i].length + 1 : 1; }

bool TextBook::load_chapter(int i, Chapter& out) {
  if (i < 0 || i >= part_count_ || file_ == nullptr) return false;
  if (loaded_ != i && format_ == Format::Comic) {
    load_comic(i);
    loaded_ = i;
  } else if (loaded_ != i) {
    loaded_ = -1;
    run_count_ = picture_count_ = 0;
    const Part& p = parts_[i];
    const uint32_t len = std::min<uint32_t>(p.length, kChapterBytes);
    if (len > 0 && file_->read_at(p.offset, raw_, len) != static_cast<int32_t>(len)) return false;
    switch (format_) {
      case Format::Text: {
        // Converted after kSpare bytes, so heading marks have room.
        char* utf8 = text_ + kSpare;
        const uint32_t n = charset::to_utf8(encoding_, raw_, len, utf8, kTextBytes - kSpare);
        paragraph_count_ = split_text(utf8, n, text_, paragraphs_, kMaxParagraphs, wrapped_, kSpare - 1);
        break;
      }
      case Format::Fb2:
      case Format::Fb2Zip: {
        const uint32_t n = charset::to_utf8(encoding_, raw_, len, text_, kTextBytes);
        text_[n] = '\0';
        uint32_t first = 0;
        if (i == annotation_) {
          for (const char* h : heading_) {
            if (h[1] != '\0') paragraphs_[first++] = h;
          }
        }
        // Contents entries in this chapter learn their paragraph from their section.
        struct Sections {
          TextBook* book;
          int chapter;
          uint32_t first;
        } sections{this, i, first};
        auto found = [](void* ctx, uint32_t n, uint32_t paragraph) {
          auto* s = static_cast<Sections*>(ctx);
          TextBook& b = *s->book;
          for (int k = 0; k < b.toc_count_; ++k) {
            if (b.toc_[k].chapter == s->chapter && b.toc_section_[k] == n) b.toc_[k].paragraph = s->first + paragraph;
          }
        };
        paragraph_count_ =
            first + fb2_paragraphs(text_, n, paragraphs_ + first, kMaxParagraphs - first, found, &sections);
        break;
      }
      case Format::Html:
      case Format::Mobi:
      case Format::Pdf:
      case Format::Docx:
      case Format::Markdown:
      case Format::Rtf: {
        const uint32_t n = charset::to_utf8(encoding_, raw_, len, text_, kTextBytes);
        text_[n] = '\0';
        // Bold, italics and pictures too.
        Markup m;
        if (runs_ != nullptr && pictures_ != nullptr) {
          m.runs = runs_;
          m.max_runs = kMaxRuns;
          m.pictures = pictures_;
          m.max_pictures = kMaxPictures;
          m.entry_of = picture_entry;
          m.ctx = this;
        }
        picture_paths_used_ = 0;
        picture_count_now_ = 0;
        paragraph_count_ = Epub::to_paragraphs(text_, n, paragraphs_, kMaxParagraphs, {}, &m);
        run_count_ = m.run_count;
        picture_count_ = m.picture_count;
        measure_pictures();
        break;
      }
      case Format::Comic:
      case Format::None:
        return false;
    }
    for (int k = 0; k < toc_count_; ++k) {
      if (toc_[k].chapter == i && toc_[k].paragraph == TocEntry::kUnknown) toc_[k].paragraph = 0;  // not found
    }
    loaded_ = i;
  }
  out = {paragraphs_, paragraph_count_, "", ""};
  return true;
}

int32_t TextBook::read_cover(const uint8_t** data) {
  if (file_ == nullptr || cover_len_ == 0) return -1;
  loaded_ = -1;  // the cover is decoded into the chapter's buffer
  auto* out = reinterpret_cast<uint8_t*>(text_);
  uint32_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (uint32_t done = 0; done < cover_len_;) {
    const uint32_t want = std::min<uint32_t>(cover_len_ - done, kChapterBytes);
    if (file_->read_at(cover_at_ + done, raw_, want) != static_cast<int32_t>(want)) return -1;
    for (uint32_t k = 0; k < want; ++k) {
      const int v = base64_value(raw_[k]);
      if (v < 0) continue;  // line breaks, padding
      acc = (acc << 6 | static_cast<uint32_t>(v)) & 0xFFFFFF;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        if (n >= kTextBytes) return -1;  // too large
        out[n++] = static_cast<uint8_t>(acc >> bits);
      }
    }
    done += want;
  }
  *data = out;
  return static_cast<int32_t>(n);
}

}  // namespace vellum

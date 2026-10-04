#include "vellum/dictionary.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "miniz.h"
#include "vellum/text.h"
#include "vellum/textfile.h"
#include "vellum/xml.h"

namespace vellum {
namespace dict {
namespace {

// ---- Words ---------------------------------------------------------------------

uint32_t lower_cp(uint32_t c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;  // Latin-1
  // Latin Extended-A pairs capital and small letters, even-odd or odd-even.
  if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c | 1;
  if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
  return c;
}

// Letters and digits; everything else is punctuation around a word.
bool word_cp(uint32_t c) {
  if (c < 0x80) return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
  if (c < 0xC0) return false;                    // Latin-1 punctuation: « » ¡ ¿ °, the soft hyphen
  if (c == 0xD7 || c == 0xF7) return false;      // × ÷
  if (c >= 0x2000 && c <= 0x206F) return false;  // dashes, quotes, ellipsis
  if (c >= 0x3000 && c <= 0x303F) return false;
  return c != 0xFFFD;
}

bool push(char (*out)[kFormBytes], int& n, int max, std::string_view s) {
  if (n >= max || s.empty() || s.size() >= kFormBytes) return false;
  for (int i = 0; i < n; ++i) {
    if (s == out[i]) return false;
  }
  std::memcpy(out[n], s.data(), s.size());
  out[n][s.size()] = '\0';
  ++n;
  return true;
}

bool vowel(char c) { return std::strchr("aeiouy", c) != nullptr; }

// English words without a common ending, from the lowercase word `w`.
void english_stems(std::string_view w, char (*out)[kFormBytes], int& n, int max) {
  char buf[kFormBytes];
  auto stem = [&](size_t cut, const char* add) {
    if (w.size() <= cut + 1) return;
    const size_t keep = w.size() - cut;
    const size_t extra = std::strlen(add);
    if (keep + extra >= sizeof(buf)) return;
    std::memcpy(buf, w.data(), keep);
    std::memcpy(buf + keep, add, extra);
    push(out, n, max, std::string_view(buf, keep + extra));
  };
  // "stopped", "running": the doubled last letter goes too.
  auto undouble = [&](size_t cut) {
    if (w.size() < cut + 3) return;
    const size_t keep = w.size() - cut;
    const char a = w[keep - 1];
    if (a == w[keep - 2] && !vowel(a) && a != 'l' && a != 's') {
      push(out, n, max, w.substr(0, keep - 1));
    }
  };
  auto ends = [&](const char* s) { return w.ends_with(s); };
  if (ends("'s") || ends("\xE2\x80\x99s")) stem(ends("'s") ? 2 : 4, "");
  if (ends("ies")) stem(3, "y");
  if (ends("es")) stem(2, "");
  if (ends("s") && !ends("ss")) stem(1, "");
  if (ends("ied")) stem(3, "y");
  if (ends("ed")) {
    stem(2, "");
    stem(1, "");  // "loved" -> "love"
    undouble(2);
  }
  if (ends("ing")) {
    stem(3, "");
    stem(3, "e");  // "making" -> "make"
    undouble(3);
  }
}

}  // namespace

void to_lower(std::string_view s, char* out, size_t size) {
  if (size == 0) return;
  size_t n = 0;
  size_t i = 0;
  while (i < s.size()) {
    char enc[4];
    const size_t len = xml::encode_utf8(lower_cp(decode_utf8(s, i)), enc);
    if (n + len >= size) break;
    std::memcpy(out + n, enc, len);
    n += len;
  }
  out[n] = '\0';
}

int word_forms(std::string_view word, const char* language, char (*out)[kFormBytes], int max) {
  // Trim punctuation from both ends and soft hyphens from inside.
  char clean[kFormBytes];
  size_t n = 0;
  size_t i = 0;
  size_t last_word_end = 0;
  bool started = false;
  while (i < word.size()) {
    const size_t at = i;
    const uint32_t c = decode_utf8(word, i);
    if (c == 0xAD) continue;
    const bool letter = word_cp(c);
    if (!started && !letter) continue;
    started = true;
    if (n + (i - at) >= sizeof(clean)) break;
    std::memcpy(clean + n, word.data() + at, i - at);
    n += i - at;
    if (letter) last_word_end = n;
  }
  n = last_word_end;
  int count = 0;
  if (n == 0) return 0;
  const std::string_view w(clean, n);
  push(out, count, max, w);
  char lower[kFormBytes];
  to_lower(w, lower, sizeof(lower));
  push(out, count, max, lower);
  if (language != nullptr && (language[0] | 32) == 'e' && (language[1] | 32) == 'n') {
    english_stems(lower, out, count, max);
  }
  return count;
}

size_t markup_to_text(std::string_view in, char* out, size_t size, bool plain) {
  if (size == 0) return 0;
  size_t n = 0;
  bool space = false;  // a space is due before the next word
  bool line = false;   // a line break is due
  int skip_depth = 0;  // inside <k>, <script> or <style>: left out
  auto put = [&](const char* s, size_t len) {
    if (len == 0) return;
    if (n > 0 && line) {
      if (n + 1 < size) out[n++] = '\n';
    } else if (n > 0 && space) {
      if (n + 1 < size) out[n++] = ' ';
    }
    line = space = false;
    if (n + len >= size) len = size - 1 - n;
    std::memcpy(out + n, s, len);
    n += len;
  };
  size_t i = 0;
  while (i < in.size() && n + 1 < size) {
    const char c = in[i];
    if (c == '<' && !plain) {
      const size_t close = in.find('>', i);
      if (close == std::string_view::npos) break;
      std::string_view tag = in.substr(i + 1, close - i - 1);
      i = close + 1;
      const bool closing = !tag.empty() && tag[0] == '/';
      if (closing) tag.remove_prefix(1);
      size_t name_end = 0;
      while (name_end < tag.size() && tag[name_end] != ' ' && tag[name_end] != '/' && tag[name_end] != '\t' &&
             tag[name_end] != '\n') {
        ++name_end;
      }
      const std::string_view name = tag.substr(0, name_end);
      if (xml::name_is(name, "k") || xml::name_is(name, "script") || xml::name_is(name, "style")) {
        skip_depth += closing ? -1 : (tag.ends_with("/") ? 0 : 1);
        if (skip_depth < 0) skip_depth = 0;
        continue;
      }
      static constexpr const char* kBreaks[] = {"br", "p",  "div", "li", "tr",         "h1",  "h2", "h3", "h4",
                                                "h5", "h6", "dd",  "dt", "blockquote", "def", "ol", "ul"};
      for (const char* b : kBreaks) {
        if (xml::name_is(name, b)) line = n > 0;
      }
      continue;
    }
    if (skip_depth > 0) {
      ++i;
      continue;
    }
    if (c == '\n' || c == '\r') {
      line = n > 0;
      ++i;
      continue;
    }
    if (c == ' ' || c == '\t') {
      space = n > 0;
      ++i;
      continue;
    }
    if (c == '&' && !plain) {
      char enc[8];
      size_t len = 0;
      if (xml::decode_entity(in, i, enc, len)) {
        if (len == 2 && static_cast<uint8_t>(enc[0]) == 0xC2 && static_cast<uint8_t>(enc[1]) == 0xA0) {
          space = n > 0;  // &nbsp;
        } else {
          put(enc, len);
        }
        continue;
      }
    }
    // A run of ordinary text.
    size_t end = i + 1;
    while (end < in.size() && std::strchr(plain ? "\n\r \t" : "<&\n\r \t", in[end]) == nullptr) ++end;
    put(in.data() + i, end - i);
    i = end;
  }
  // Never end inside a UTF-8 character.
  while (n > 0 && (static_cast<uint8_t>(out[n - 1]) & 0xC0) == 0x80) {
    size_t start = n - 1;
    while (start > 0 && (static_cast<uint8_t>(out[start]) & 0xC0) == 0x80) --start;
    const uint8_t lead = static_cast<uint8_t>(out[start]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    if (n - start >= need) break;
    n = start;
  }
  out[n] = '\0';
  return n;
}

void language_of(std::string_view path, char* out, size_t size) {
  struct Name {
    const char* word;
    const char* code;
  };
  static constexpr Name kNames[] = {
      {"en", "en"},       {"eng", "en"},      {"english", "en"}, {"lt", "lt"},  {"lit", "lt"}, {"lithuanian", "lt"},
      {"lietuviu", "lt"}, {"lietuvių", "lt"}, {"de", "de"},      {"deu", "de"}, {"ger", "de"}, {"german", "de"},
      {"fr", "fr"},       {"fra", "fr"},      {"french", "fr"},  {"es", "es"},  {"spa", "es"}, {"spanish", "es"},
      {"ru", "ru"},       {"rus", "ru"},      {"russian", "ru"}, {"pl", "pl"},  {"pol", "pl"}, {"polish", "pl"},
      {"lv", "lv"},       {"lav", "lv"},      {"latvian", "lv"}, {"it", "it"},  {"ita", "it"}, {"italian", "it"},
      {"uk", "uk"},       {"ukr", "uk"},      {"nl", "nl"},      {"pt", "pt"},  {"sv", "sv"},  {"fi", "fi"},
      {"da", "da"},       {"cs", "cs"},       {"et", "et"},
  };
  // The file name says most, then its folders, nearest first.
  char lower[256];
  to_lower(path, lower, sizeof(lower));
  std::string_view rest(lower);
  if (out != nullptr && size > 0) out[0] = '\0';
  while (!rest.empty()) {
    const size_t slash = rest.rfind('/');
    std::string_view part = slash == std::string_view::npos ? rest : rest.substr(slash + 1);
    rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(0, slash);
    while (!part.empty()) {
      size_t end = 0;
      while (end < part.size() && part[end] != '-' && part[end] != '_' && part[end] != '.' && part[end] != ' ' &&
             part[end] != '(' && part[end] != ')') {
        ++end;
      }
      const std::string_view token = part.substr(0, end);
      for (const Name& name : kNames) {
        if (token == name.word) {
          textfile::copy(name.code, out, size);
          return;
        }
      }
      part = end < part.size() ? part.substr(end + 1) : std::string_view{};
    }
  }
}

}  // namespace dict

namespace {

constexpr uint32_t kMagic = 0x31584456;  // "VDX1", little-endian
constexpr uint32_t kScanBytes = 4096;
constexpr uint32_t kMaxWord = 256;  // StarDict's limit, with its NUL
constexpr uint32_t kRawBytes = 16 * 1024;
constexpr uint32_t kOutBytes = 65536;  // dictzip chunks are smaller
constexpr int kMaxChunks = 65535;

uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
         p[3];
}
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

char fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

// StarDict's order: ASCII letters without case, then byte by byte.
int ascii_casecmp(const char* a, const char* b) {
  for (;; ++a, ++b) {
    const unsigned char x = static_cast<unsigned char>(fold(*a));
    const unsigned char y = static_cast<unsigned char>(fold(*b));
    if (x != y || x == 0) return x - y;
  }
}

bool has_suffix(const char* name, const char* ext) {
  const size_t n = std::strlen(name);
  const size_t e = std::strlen(ext);
  if (n < e) return false;
  for (size_t i = 0; i < e; ++i) {
    if (fold(name[n - e + i]) != ext[i]) return false;
  }
  return true;
}

bool file_exists(Storage& s, const char* path) {
  File* f = s.open(path);
  if (f == nullptr) return false;
  s.close(f);
  return true;
}

// Opens and closes files of one dictionary around a lookup.
struct Opened {
  Storage& storage;
  File* file;
  Opened(Storage& s, const char* path) : storage(s), file(s.open(path)) {}
  ~Opened() {
    if (file != nullptr) storage.close(file);
  }
};

}  // namespace

Dictionaries::Dictionaries()
    : entries_(nullptr),
      block_(nullptr),
      samples_(nullptr),
      coarse_(nullptr),
      out_(nullptr),
      in_(nullptr),
      inflater_(nullptr),
      raw_(nullptr),
      scan_(nullptr) {}

Dictionaries::~Dictionaries() {
  delete[] entries_;
  delete[] block_;
  delete[] coarse_;
  delete[] out_;
  delete[] in_;
  delete static_cast<tinfl_decompressor*>(inflater_);
  delete[] raw_;
  delete[] scan_;
}

// Buffers come the first time a card has a dictionary, and stay.
bool Dictionaries::allocate() {
  if (entries_ == nullptr) entries_ = new (std::nothrow) Entry[kMax];
  if (block_ == nullptr) block_ = new (std::nothrow) uint32_t[4 + kMaxSamples];
  samples_ = block_ != nullptr ? block_ + 4 : nullptr;
  if (coarse_ == nullptr) coarse_ = new (std::nothrow) uint32_t[kMaxChunks / kCoarse + 1];
  if (out_ == nullptr) out_ = new (std::nothrow) uint8_t[kOutBytes];
  if (in_ == nullptr) in_ = new (std::nothrow) uint8_t[kScanBytes];
  if (inflater_ == nullptr) inflater_ = new (std::nothrow) tinfl_decompressor;
  if (raw_ == nullptr) raw_ = new (std::nothrow) char[kRawBytes + 1];
  if (scan_ == nullptr) scan_ = new (std::nothrow) uint8_t[kScanBytes];
  return entries_ != nullptr && block_ != nullptr && coarse_ != nullptr && out_ != nullptr && in_ != nullptr &&
         inflater_ != nullptr && raw_ != nullptr && scan_ != nullptr;
}

void Dictionaries::scan(Storage* storage) {
  storage_ = storage;
  count_ = 0;
  loaded_ = -1;
  if (storage == nullptr) return;
  // Names first, then a look inside: opening files while listing would
  // reuse the storage's path buffer.
  struct Found {
    char path[160];
    bool dir;
  };
  struct Listing {
    Found items[2 * kMax];
    int n = 0;
    const char* dir;
  };
  static Listing* listing = new (std::nothrow) Listing;
  static Listing* inner = new (std::nothrow) Listing;
  if (listing == nullptr || inner == nullptr) return;
  auto collect = [](void* ctx, const char* name, bool is_dir, uint32_t) {
    auto* l = static_cast<Listing*>(ctx);
    if (name[0] == '.' || l->n >= 2 * kMax) return;
    if (!is_dir && !has_suffix(name, ".ifo")) return;
    Found& f = l->items[l->n];
    if (std::snprintf(f.path, sizeof(f.path), "%s/%s", l->dir, name) >= static_cast<int>(sizeof(f.path))) return;
    f.dir = is_dir;
    ++l->n;
  };
  listing->n = 0;
  listing->dir = kFolder;
  if (!storage->list(kFolder, collect, listing)) return;
  if (!allocate()) return;
  for (int k = 0; k < listing->n; ++k) {
    const Found& f = listing->items[k];
    if (!f.dir) {
      add(*storage, f.path);
      continue;
    }
    inner->n = 0;
    inner->dir = f.path;
    if (!storage->list(f.path, collect, inner)) continue;
    for (int j = 0; j < inner->n; ++j) {
      if (!inner->items[j].dir) add(*storage, inner->items[j].path);
    }
  }
  // A stable order: by path.
  std::sort(entries_, entries_ + count_,
            [](const Entry& a, const Entry& b) { return std::strcmp(a.base, b.base) < 0; });
}

void Dictionaries::add(Storage& storage, const char* ifo_path) {
  if (count_ >= kMax) return;
  char text[2048];
  const int32_t len = textfile::read(storage, ifo_path, text, sizeof(text));
  if (len <= 0 || std::strncmp(text, "StarDict's dict ifo file", 24) != 0) return;
  Entry& e = entries_[count_];
  e = {};
  const size_t base_len = std::strlen(ifo_path) - 4;
  if (base_len >= sizeof(e.base)) return;
  std::memcpy(e.base, ifo_path, base_len);
  e.base[base_len] = '\0';
  char lang[8] = "";
  std::string_view rest(text, static_cast<size_t>(len));
  while (!rest.empty()) {
    const size_t end = rest.find('\n');
    std::string_view line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, eq);
    const std::string_view value = line.substr(eq + 1);
    if (key == "bookname") textfile::copy(value, e.name, sizeof(e.name));
    if (key == "wordcount") e.words = textfile::to_uint(value);
    if (key == "idxfilesize") e.idx_size = textfile::to_uint(value);
    if (key == "idxoffsetbits") e.offset64 = value == "64";
    if (key == "sametypesequence") textfile::copy(value, e.types, sizeof(e.types));
    if (key == "lang") dict::language_of(value, lang, sizeof(lang));
  }
  char path[176];
  std::snprintf(path, sizeof(path), "%s.idx", e.base);
  if (!file_exists(storage, path)) return;  // a compressed .idx.gz is not read
  std::snprintf(path, sizeof(path), "%s.dict", e.base);
  if (!file_exists(storage, path)) {
    std::snprintf(path, sizeof(path), "%s.dict.dz", e.base);
    if (!file_exists(storage, path)) return;
    e.dz = true;
  }
  if (lang[0] == '\0') dict::language_of(e.base + std::strlen(kFolder) + 1, lang, sizeof(lang));
  if (lang[0] == '\0') dict::language_of(e.name, lang, sizeof(lang));
  textfile::copy(lang, e.lang, sizeof(e.lang));
  if (e.name[0] == '\0') {
    const char* slash = std::strrchr(e.base, '/');
    textfile::copy(slash != nullptr ? slash + 1 : e.base, e.name, sizeof(e.name));
  }
  ++count_;
}

void Dictionaries::index_path(const Entry& e, char* out, size_t size) const {
  uint32_t h = 2166136261u;  // FNV-1a of the path: one index per dictionary
  for (const char* p = e.base; *p != '\0'; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  std::snprintf(out, size, "%s/%08lx.vdx", kIndexFolder, static_cast<unsigned long>(h));
}

bool Dictionaries::read_entry(File& idx, uint32_t at, char* word, uint64_t& offset, uint32_t& size, uint32_t& next) {
  const uint32_t tail = idx.size() > at ? idx.size() - at : 0;
  const uint32_t want = std::min(tail, kMaxWord + 12);
  uint8_t buf[kMaxWord + 12];
  if (want == 0 || idx.read_at(at, buf, want) != static_cast<int32_t>(want)) return false;
  const void* nul = std::memchr(buf, 0, std::min(want, kMaxWord));
  if (nul == nullptr) return false;
  const uint32_t len = static_cast<uint32_t>(static_cast<const uint8_t*>(nul) - buf);
  const uint32_t number = entries_[loaded_].offset64 ? 8 : 4;
  if (len + 1 + number + 4 > want) return false;
  std::memcpy(word, buf, len + 1);
  const uint8_t* p = buf + len + 1;
  offset = number == 8 ? (uint64_t{be32(p)} << 32 | be32(p + 4)) : be32(p);
  size = be32(p + number);
  next = at + len + 1 + number + 4;
  return true;
}

bool Dictionaries::build_index(File& idx, const Entry& e) {
  // Every stride_-th word, the stride doubling whenever the samples run out.
  stride_ = std::max<uint32_t>(1, (e.words + kMaxSamples - 1) / kMaxSamples);
  sample_count_ = 0;
  const uint32_t number = e.offset64 ? 8 : 4;
  const uint32_t total = idx.size();
  uint32_t pos = 0;
  uint32_t k = 0;
  while (pos < total) {
    const uint32_t want = std::min(total - pos, kScanBytes);
    if (idx.read_at(pos, scan_, want) != static_cast<int32_t>(want)) return false;
    uint32_t i = 0;
    for (;;) {
      const void* nul = std::memchr(scan_ + i, 0, want - i);
      if (nul == nullptr) break;
      const uint32_t end = static_cast<uint32_t>(static_cast<const uint8_t*>(nul) - scan_) + 1 + number + 4;
      if (end > want) break;
      if (k % stride_ == 0) {
        if (sample_count_ == kMaxSamples) {
          for (int s = 0; s < kMaxSamples / 2; ++s) samples_[s] = samples_[2 * s];
          sample_count_ = kMaxSamples / 2;
          stride_ *= 2;
        }
        if (k % stride_ == 0) samples_[sample_count_++] = pos + i;
      }
      ++k;
      i = end;
    }
    if (i == 0) return false;  // an entry longer than the buffer: not a word list
    pos += i;
  }
  block_[0] = kMagic;
  block_[1] = total;
  block_[2] = stride_;
  block_[3] = static_cast<uint32_t>(sample_count_);
  return true;
}

bool Dictionaries::load(int i, SlowFn slow, void* ctx) {
  if (loaded_ == i) return true;
  loaded_ = -1;
  inflated_ = -1;
  const Entry& e = entries_[i];
  char path[176];
  std::snprintf(path, sizeof(path), "%s.idx", e.base);
  Opened idx(*storage_, path);
  if (idx.file == nullptr) return false;
  loaded_ = i;  // read_entry() reads its number size

  // The index from an earlier lookup, when it still matches the word list.
  char cache[64];
  index_path(e, cache, sizeof(cache));
  bool ready = false;
  if (Opened f(*storage_, cache); f.file != nullptr && f.file->size() >= 16) {
    const uint32_t bytes = f.file->size();
    if (bytes <= (4 + kMaxSamples) * 4 && f.file->read_at(0, block_, bytes) == static_cast<int32_t>(bytes) &&
        block_[0] == kMagic && block_[1] == idx.file->size() && block_[3] <= kMaxSamples &&
        16 + block_[3] * 4 == bytes) {
      stride_ = block_[2];
      sample_count_ = static_cast<int>(block_[3]);
      ready = true;
    }
  }
  if (!ready) {
    if (slow != nullptr) slow(ctx);
    if (!build_index(*idx.file, e)) {
      loaded_ = -1;
      return false;
    }
    storage_->write(cache, block_, 16 + static_cast<uint32_t>(sample_count_) * 4);
  }
  if (e.dz) {
    std::snprintf(path, sizeof(path), "%s.dict.dz", e.base);
    Opened dz(*storage_, path);
    if (dz.file == nullptr || !open_dz(*dz.file)) {
      loaded_ = -1;
      return false;
    }
  }
  return true;
}

bool Dictionaries::find(const char* key, char* headword, uint64_t& offset, uint32_t& size) {
  const Entry& e = entries_[loaded_];
  char path[176];
  std::snprintf(path, sizeof(path), "%s.idx", e.base);
  Opened idx(*storage_, path);
  if (idx.file == nullptr || sample_count_ == 0) return false;
  char word[kMaxWord];
  uint64_t off;
  uint32_t sz, next;
  // The first sample not before the key, without case; the run of words
  // that may match starts in the stretch before it.
  int lo = 0, hi = sample_count_;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (!read_entry(*idx.file, samples_[mid], word, off, sz, next)) return false;
    if (ascii_casecmp(word, key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  uint32_t at = samples_[lo > 0 ? lo - 1 : 0];
  bool found = false;
  while (at < idx.file->size()) {
    if (!read_entry(*idx.file, at, word, off, sz, next)) break;
    const int c = ascii_casecmp(word, key);
    if (c > 0) break;
    if (c == 0) {
      // The same letters: take the exact spelling, else the first.
      const bool exact = std::strcmp(word, key) == 0;
      if (exact || !found) {
        textfile::copy(word, headword, dict::kFormBytes);
        offset = off;
        size = sz;
        found = true;
      }
      if (exact) break;
    }
    at = next;
  }
  return found;
}

bool Dictionaries::open_dz(File& f) {
  // gzip header with dictzip's "RA" field: chunk length, count and sizes.
  uint8_t h[12];
  if (f.read_at(0, h, 12) != 12 || h[0] != 0x1F || h[1] != 0x8B || h[2] != 8 || !(h[3] & 4)) return false;
  const uint8_t flags = h[3];
  const uint32_t xlen = le16(h + 10);
  uint32_t pos = 12;
  bool found = false;
  while (pos + 4 <= 12 + xlen) {
    uint8_t sub[10];
    if (f.read_at(pos, sub, 10) != 10) return false;
    const uint32_t len = le16(sub + 2);
    if (sub[0] == 'R' && sub[1] == 'A' && len >= 6) {
      chunk_len_ = le16(sub + 6);
      chunk_count_ = le16(sub + 8);
      sizes_at_ = pos + 10;
      found = true;
    }
    pos += 4 + len;
  }
  if (!found || chunk_len_ == 0 || chunk_len_ > kOutBytes || 6 + chunk_count_ * 2 > xlen) return false;
  pos = 12 + xlen;
  // Past the name and comment, when stored, and the header check.
  for (const uint8_t flag : {uint8_t{8}, uint8_t{16}}) {
    if (!(flags & flag)) continue;
    for (;;) {
      const int32_t n = f.read_at(pos, in_, kScanBytes);
      if (n <= 0) return false;
      const void* nul = std::memchr(in_, 0, static_cast<size_t>(n));
      if (nul != nullptr) {
        pos += static_cast<uint32_t>(static_cast<const uint8_t*>(nul) - in_) + 1;
        break;
      }
      pos += static_cast<uint32_t>(n);
    }
  }
  if (flags & 2) pos += 2;
  // Where every kCoarse-th chunk starts.
  uint32_t at = pos;
  for (uint32_t c = 0; c < chunk_count_; c += kCoarse) {
    coarse_[c / kCoarse] = at;
    const uint32_t n = std::min<uint32_t>(kCoarse, chunk_count_ - c);
    if (f.read_at(sizes_at_ + c * 2, in_, n * 2) != static_cast<int32_t>(n * 2)) return false;
    for (uint32_t k = 0; k < n; ++k) at += le16(in_ + 2 * k);
  }
  return true;
}

bool Dictionaries::read_dz_chunk(uint32_t chunk) {
  if (static_cast<int32_t>(chunk) == inflated_) return true;
  inflated_ = -1;
  if (chunk >= chunk_count_) return false;
  char path[176];
  std::snprintf(path, sizeof(path), "%s.dict.dz", entries_[loaded_].base);
  Opened f(*storage_, path);
  if (f.file == nullptr) return false;
  // Its place: the coarse mark, then the sizes of the chunks after it.
  const uint32_t first = chunk / kCoarse * kCoarse;
  uint8_t sizes[2 * kCoarse];
  const uint32_t n = chunk - first + 1;
  if (f.file->read_at(sizes_at_ + first * 2, sizes, n * 2) != static_cast<int32_t>(n * 2)) return false;
  uint32_t pos = coarse_[chunk / kCoarse];
  for (uint32_t k = 0; k + 1 < n; ++k) pos += le16(sizes + 2 * k);
  uint32_t remaining = le16(sizes + 2 * (n - 1));

  // Each chunk is a raw deflate stream flushed to a byte boundary, so it
  // inflates on its own.
  auto* inflater = static_cast<tinfl_decompressor*>(inflater_);
  tinfl_init(inflater);
  size_t written = 0;
  while (remaining > 0) {
    const uint32_t want = std::min(remaining, kScanBytes);
    if (f.file->read_at(pos, in_, want) != static_cast<int32_t>(want)) return false;
    pos += want;
    remaining -= want;
    size_t used = 0;
    while (used < want) {
      size_t in_size = want - used;
      size_t out_size = kOutBytes - written;
      const tinfl_status status =
          tinfl_decompress(inflater, in_ + used, &in_size, out_, out_ + written, &out_size,
                           TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | TINFL_FLAG_HAS_MORE_INPUT);
      used += in_size;
      written += out_size;
      if (status == TINFL_STATUS_DONE) {
        remaining = 0;
        break;
      }
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT) break;
      if (status < TINFL_STATUS_DONE || status == TINFL_STATUS_HAS_MORE_OUTPUT) return false;
    }
  }
  inflated_ = static_cast<int32_t>(chunk);
  inflated_len_ = static_cast<uint32_t>(written);
  return true;
}

int32_t Dictionaries::read_data(uint64_t offset, uint32_t size) {
  size = std::min(size, kRawBytes);
  const Entry& e = entries_[loaded_];
  if (!e.dz) {
    char path[176];
    std::snprintf(path, sizeof(path), "%s.dict", e.base);
    Opened f(*storage_, path);
    if (f.file == nullptr || offset > 0xFFFFFFFFu) return -1;
    return f.file->read_at(static_cast<uint32_t>(offset), raw_, size);
  }
  uint32_t done = 0;
  while (done < size) {
    const uint64_t at = offset + done;
    const uint32_t chunk = static_cast<uint32_t>(at / chunk_len_);
    const uint32_t in_chunk = static_cast<uint32_t>(at % chunk_len_);
    if (!read_dz_chunk(chunk) || in_chunk >= inflated_len_) return done > 0 ? static_cast<int32_t>(done) : -1;
    const uint32_t take = std::min(size - done, inflated_len_ - in_chunk);
    std::memcpy(raw_ + done, out_ + in_chunk, take);
    done += take;
  }
  return static_cast<int32_t>(done);
}

bool Dictionaries::lookup(std::string_view word, const char* language, const char* fallback, Result& result, char* text,
                          size_t cap, SlowFn slow, void* ctx) {
  if (count_ == 0 || cap == 0) return false;
  text[0] = '\0';
  // The book's language first, then the fallback, then the rest.
  auto same = [](const char* a, const char* b) {
    return a != nullptr && b != nullptr && a[0] != '\0' && fold(a[0]) == fold(b[0]) && fold(a[1]) == fold(b[1]);
  };
  int order[kMax];
  int n = 0;
  for (int pass = 0; pass < 3; ++pass) {
    for (int i = 0; i < count_; ++i) {
      const bool book = same(entries_[i].lang, language);
      const bool second = !book && same(entries_[i].lang, fallback);
      if ((pass == 0 && book) || (pass == 1 && second) || (pass == 2 && !book && !second)) order[n++] = i;
    }
  }
  char forms[12][dict::kFormBytes];
  for (int k = 0; k < n; ++k) {
    const Entry& e = entries_[order[k]];
    const int count = dict::word_forms(word, e.lang[0] != '\0' ? e.lang : language, forms, 12);
    if (count == 0) return false;
    if (!load(order[k], slow, ctx)) continue;
    for (int f = 0; f < count; ++f) {
      uint64_t offset;
      uint32_t size;
      if (!find(forms[f], result.headword, offset, size)) continue;
      const int32_t got = read_data(offset, size);
      if (got < 0) continue;
      textfile::copy(e.name, result.dictionary, sizeof(result.dictionary));
      // The fields: with sametypesequence their types are given once and the
      // last one runs to the end; otherwise each starts with its type.
      const std::string_view data(raw_, static_cast<size_t>(got));
      size_t pos = 0;
      size_t len = 0;
      const size_t fixed = std::strlen(e.types);
      for (size_t field = 0; pos < data.size() && (fixed == 0 || field < fixed); ++field) {
        char type = fixed > 0 ? e.types[field] : data[pos++];
        const bool last = fixed > 0 && field + 1 == fixed;
        std::string_view body;
        if (type >= 'a' && type <= 'z') {
          const size_t end = last ? data.size() : std::min(data.find('\0', pos), data.size());
          body = data.substr(pos, end - pos);
          pos = end + 1;
        } else {
          // Binary fields (sounds, pictures) carry their size: skip them.
          uint32_t bytes = static_cast<uint32_t>(data.size() - pos);
          if (!last) {
            if (pos + 4 > data.size()) break;
            bytes = be32(reinterpret_cast<const uint8_t*>(data.data() + pos));
            pos += 4;
          }
          pos += bytes;
          continue;
        }
        if (cap - len < 8) break;  // full
        if (len > 0) text[len++] = '\n';
        if (type == 't') {
          // Pronunciation, set off in brackets.
          text[len++] = '[';
          len += dict::markup_to_text(body, text + len, cap - len - 1);
          text[len++] = ']';
          text[len] = '\0';
        } else {
          // Markup (HTML, Pango, XDXF) loses its tags; plain text keeps its
          // line breaks and is tidied the same way.
          len += dict::markup_to_text(body, text + len, cap - len, std::strchr("ghxw", type) == nullptr);
        }
      }
      result.length = len;
      return true;
    }
  }
  return false;
}

}  // namespace vellum

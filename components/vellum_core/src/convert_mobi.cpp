// Kindle books without DRM: MOBI (Mobipocket, "KF7"), KF8 (.azw3) and the
// older PalmDOC text files (.pdb, .prc), all Palm databases of records.
//
// Text records are decompressed (PalmDOC LZ77 or Huffman/CDIC) one at a
// time and their HTML rewritten for TextBook: the head left out, pictures
// (image records) written next to the page, the book's contents (its NCX
// index) turned into <vellum-chapter> marks. A KF8 book stores each XHTML
// file as a skeleton and fragments to insert; its text is first unpacked to
// the card, then each file is put together in memory and rewritten.
#include <cstdio>
#include <cstring>
#include <new>

#include "convert_internal.h"
#include "vellum/charset.h"

namespace vellum::convert {
namespace {

constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr uint32_t kMaxRecords = 16384;
constexpr int kMaxMarks = 1024;
constexpr int kMaxImages = 8192;
constexpr uint32_t kRecordBytes = 64 * 1024;  // a text or index record
constexpr uint32_t kTextBytes = 16 * 1024;    // a text record, decompressed
constexpr uint32_t kImageBytes = 512 * 1024;  // the largest picture written
constexpr uint32_t kRewriterBytes = 8 * 1024;
constexpr uint32_t kMaxFiles = 2048;  // KF8 skeletons
constexpr uint32_t kMaxFrags = 8192;  // and fragments
constexpr int kMaxTags = 32;

// A contents entry: where in the text (or, in KF8, which file and where in
// it) a chapter begins.
struct Mark {
  uint32_t pos;
  uint32_t off;  // KF8: from the fragment's start
  uint16_t file;
  uint8_t depth;
  char title[96];
};

class Pdb {
 public:
  Pdb(File& f, uint32_t* offsets) : f_(f), off_(offsets) {}

  bool open() {
    uint8_t h[78];
    if (f_.size() < 78 || f_.read_at(0, h, 78) != 78) return false;
    std::memcpy(type_, h + 60, 8);
    count_ = be16(h + 76);
    if (count_ == 0 || count_ >= kMaxRecords) return false;
    uint8_t e[8];
    for (uint32_t i = 0; i < count_; ++i) {
      if (f_.read_at(78 + 8 * i, e, 8) != 8) return false;
      off_[i] = be32(e);
    }
    off_[count_] = f_.size();
    for (uint32_t i = 0; i < count_; ++i) {
      if (off_[i] > off_[i + 1] || off_[i + 1] > f_.size()) return false;
    }
    std::memcpy(name_, h, 32);
    name_[32] = '\0';
    return true;
  }
  uint32_t count() const { return count_; }
  uint32_t size(uint32_t i) const { return i < count_ ? off_[i + 1] - off_[i] : 0; }
  // Reads record i into buf (at most cap bytes); its length, or -1.
  int32_t read(uint32_t i, uint8_t* buf, uint32_t cap) const {
    if (i >= count_) return -1;
    const uint32_t n = std::min(size(i), cap);
    return f_.read_at(off_[i], buf, n) == static_cast<int32_t>(n) ? static_cast<int32_t>(n) : -1;
  }
  bool is(const char* type8) const { return std::memcmp(type_, type8, 8) == 0; }
  const char* name() const { return name_; }

 private:
  File& f_;
  uint32_t* off_;
  uint32_t count_ = 0;
  char type_[8] = {};
  char name_[33] = {};
};

// ---- Decompressing ---------------------------------------------------------------

uint32_t palmdoc(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap) {
  uint32_t w = 0;
  for (uint32_t i = 0; i < len && w < cap;) {
    const uint8_t c = in[i++];
    if (c == 0 || (c >= 9 && c <= 0x7F)) {
      out[w++] = c;
    } else if (c <= 8) {
      for (uint32_t k = 0; k < c && i < len && w < cap; ++k) out[w++] = in[i++];
    } else if (c <= 0xBF) {
      if (i >= len) break;
      const uint32_t m = ((uint32_t{c} << 8) | in[i++]) & 0x3FFF;
      const uint32_t dist = m >> 3, n = (m & 7) + 3;
      if (dist == 0 || dist > w) break;
      for (uint32_t k = 0; k < n && w < cap; ++k, ++w) out[w] = out[w - dist];
    } else {
      if (w + 2 > cap) break;
      out[w++] = ' ';
      out[w++] = c ^ 0x80;
    }
  }
  return w;
}

// Huffman/CDIC, as Mobipocket Creator wrote with its highest compression.
class Huff {
 public:
  // pool: where the CDIC records are kept; index: a phrase's place in it.
  bool load(const Pdb& pdb, uint32_t huff, uint32_t count, uint8_t* pool, uint32_t pool_size, uint32_t* index,
            uint32_t max_phrases) {
    if (pdb.size(huff) < 24) return false;
    const int32_t n = pdb.read(huff, pool, pool_size);
    if (n < 24 || std::memcmp(pool, "HUFF", 4) != 0) return false;
    const uint32_t off1 = be32(pool + 8), off2 = be32(pool + 12);
    if (off1 + 1024 > static_cast<uint32_t>(n) || off2 + 256 > static_cast<uint32_t>(n)) return false;
    for (int i = 0; i < 256; ++i) {
      const uint32_t v = be32(pool + off1 + 4 * i);
      Code& c = dict1_[i];
      c.len = static_cast<uint8_t>(v & 0x1F);
      c.term = (v & 0x80) != 0;
      if (c.len == 0) return false;
      c.max = ((uint64_t{v >> 8} + 1) << (32 - c.len)) - 1;
    }
    min_[0] = 0;
    max_[0] = 0;
    for (int len = 1; len <= 32; ++len) {
      min_[len] = uint64_t{be32(pool + off2 + 8 * (len - 1))} << (32 - len);
      max_[len] = ((uint64_t{be32(pool + off2 + 8 * (len - 1) + 4)} + 1) << (32 - len)) - 1;
    }
    // The dictionaries, one after another.
    uint32_t used = 0;
    phrases_ = 0;
    for (uint32_t r = huff + 1; r < huff + count; ++r) {
      const uint32_t size = pdb.size(r);
      if (used + size > pool_size) return false;
      uint8_t* d = pool + used;
      if (pdb.read(r, d, size) != static_cast<int32_t>(size) || size < 16 || std::memcmp(d, "CDIC", 4) != 0) {
        return false;
      }
      const uint32_t total = be32(d + 8), bits = be32(d + 12);
      if (bits > 16) return false;
      const uint32_t here = std::min<uint32_t>(1u << bits, total > phrases_ ? total - phrases_ : 0);
      for (uint32_t k = 0; k < here; ++k) {
        if (phrases_ >= max_phrases || 16 + 2 * k + 2 > size) return false;
        const uint32_t at = 16 + be16(d + 16 + 2 * k);
        if (at + 2 > size) return false;
        index[phrases_++] = used + at;
      }
      used += size;
    }
    pool_ = pool;
    pool_used_ = used;
    index_ = index;
    return true;
  }

  // Decompresses one record; returns the bytes written.
  uint32_t unpack(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t cap) {
    out_ = out;
    cap_ = cap;
    w_ = 0;
    decode(in, len, 0);
    return w_;
  }

 private:
  struct Code {
    uint8_t len;
    bool term;
    uint64_t max;
  };

  void decode(const uint8_t* in, uint32_t len, int depth) {
    if (depth > 16) return;
    auto byte = [&](uint32_t i) -> uint64_t { return i < len ? in[i] : 0; };
    auto load = [&](uint32_t at) {
      uint64_t x = 0;
      for (uint32_t k = 0; k < 8; ++k) x = x << 8 | byte(at + k);
      return x;
    };
    int64_t bits_left = int64_t{len} * 8;
    uint32_t pos = 0;
    uint64_t x = load(pos);
    int n = 32;
    for (;;) {
      if (n <= 0) {
        pos += 4;
        x = load(pos);
        n += 32;
      }
      const uint64_t code = (x >> n) & 0xFFFFFFFFu;
      const Code& c = dict1_[code >> 24];
      int clen = c.len;
      uint64_t max = c.max;
      if (!c.term) {
        while (clen < 32 && code < min_[clen]) ++clen;
        max = max_[clen];
      }
      n -= clen;
      bits_left -= clen;
      if (bits_left < 0) break;
      const uint32_t r = static_cast<uint32_t>((max - code) >> (32 - clen));
      if (r >= phrases_) return;
      const uint32_t at = index_[r];
      if (at + 2 > pool_used_) return;
      const uint32_t head = be16(pool_ + at);
      const uint32_t plen = std::min(head & 0x7FFF, pool_used_ - at - 2);
      const uint8_t* phrase = pool_ + at + 2;
      if (head & 0x8000) {
        const uint32_t k = std::min(plen, cap_ - w_);
        std::memcpy(out_ + w_, phrase, k);
        w_ += k;
      } else {
        decode(phrase, plen, depth + 1);
      }
      if (w_ >= cap_) return;
    }
  }

  Code dict1_[256];
  uint64_t min_[33];
  uint64_t max_[33];
  const uint8_t* pool_ = nullptr;
  uint32_t pool_used_ = 0;
  const uint32_t* index_ = nullptr;
  uint32_t phrases_ = 0;
  uint8_t* out_ = nullptr;
  uint32_t cap_ = 0;
  uint32_t w_ = 0;
};

// The bytes at a text record's end that are not text: trailing entries
// named by the header's extra flags.
uint32_t text_length(const uint8_t* d, uint32_t len, uint32_t flags) {
  for (uint32_t f = flags >> 1; f != 0; f >>= 1) {
    if ((f & 1) == 0) continue;
    // Each entry ends with its own size, a number read backwards.
    uint32_t num = 0;
    const uint32_t from = len >= 4 ? len - 4 : 0;
    for (uint32_t i = from; i < len; ++i) {
      if (d[i] & 0x80) num = 0;
      num = (num << 7) | (d[i] & 0x7F);
    }
    if (num > len) return 0;
    len -= num;
  }
  if (flags & 1) {
    if (len == 0) return 0;
    const uint32_t num = (d[len - 1] & 3) + 1;
    len = num > len ? 0 : len - num;
  }
  return len;
}

// ---- Indexes (NCX, skeletons, fragments) -----------------------------------------

uint32_t varint(const uint8_t* d, uint32_t end, uint32_t& at) {
  uint32_t v = 0;
  while (at < end) {
    const uint8_t b = d[at++];
    v = (v << 7) | (b & 0x7F);
    if (b & 0x80) break;
  }
  return v;
}

struct TagValues {
  uint8_t count[kMaxTags];
  uint32_t v[kMaxTags][4];
  uint32_t get(int tag, int k, uint32_t fallback = kNone) const {
    return tag < kMaxTags && count[tag] > k ? v[tag][k] : fallback;
  }
};

class Index {
 public:
  Index(const Pdb& pdb, uint8_t* buf, uint8_t* cncx) : pdb_(pdb), buf_(buf), cncx_(cncx) {}

  // Calls fn(text, values) for each entry of the index at record `first`.
  template <typename Fn>
  bool read(uint32_t first, Fn&& fn) {
    first_ = first;
    cncx_loaded_ = kNone;
    const int32_t n = pdb_.read(first, buf_, kRecordBytes);
    if (n < 56 || std::memcmp(buf_, "INDX", 4) != 0) return false;
    const uint32_t len = be32(buf_ + 4);
    records_ = be32(buf_ + 24);
    utf8_ = be32(buf_ + 28) == 65001;
    if (len + 12 > static_cast<uint32_t>(n) || std::memcmp(buf_ + len, "TAGX", 4) != 0) return false;
    const uint32_t tagx = be32(buf_ + len + 4);
    control_bytes_ = be32(buf_ + len + 8);
    tag_count_ = 0;
    for (uint32_t i = 12; i + 4 <= tagx && len + i + 4 <= static_cast<uint32_t>(n) && tag_count_ < 64; i += 4) {
      const uint8_t* t = buf_ + len + i;
      tags_[tag_count_++] = {t[0], t[1], t[2], t[3]};
    }
    for (uint32_t r = 1; r <= records_; ++r) {
      const int32_t m = pdb_.read(first + r, buf_, kRecordBytes);
      if (m < 28 || std::memcmp(buf_, "INDX", 4) != 0) return false;
      const uint32_t idxt = be32(buf_ + 20), count = be32(buf_ + 24);
      if (idxt + 4 + 2 * count > static_cast<uint32_t>(m)) return false;
      for (uint32_t j = 0; j < count; ++j) {
        const uint32_t start = be16(buf_ + idxt + 4 + 2 * j);
        const uint32_t end = j + 1 < count ? be16(buf_ + idxt + 4 + 2 * (j + 1)) : idxt;
        if (start >= end || end > static_cast<uint32_t>(m)) continue;
        const uint32_t tlen = buf_[start];
        if (start + 1 + tlen > end) continue;
        const std::string_view text(reinterpret_cast<const char*>(buf_ + start + 1), tlen);
        TagValues tv;
        std::memset(tv.count, 0, sizeof(tv.count));
        tag_map(start + 1 + tlen, end, tv);
        fn(text, tv);
      }
    }
    return true;
  }

  // A label from the index's CNCX records, as UTF-8.
  void label(uint32_t off, char* out, size_t size) {
    out[0] = '\0';
    const uint32_t rec = first_ + records_ + 1 + (off >> 16);
    if (rec != cncx_loaded_) {
      cncx_len_ = pdb_.read(rec, cncx_, kRecordBytes);
      cncx_loaded_ = rec;
    }
    if (cncx_len_ <= 0) return;
    uint32_t at = off & 0xFFFF;
    const uint32_t end = static_cast<uint32_t>(cncx_len_);
    if (at >= end) return;
    const uint32_t len = varint(cncx_, end, at);
    const uint32_t n = std::min(len, end - at);
    if (utf8_) {
      size_t k = std::min<size_t>(n, size - 1);
      while (k > 0 && k < n && (cncx_[at + k] & 0xC0) == 0x80) --k;
      std::memcpy(out, cncx_ + at, k);
      out[k] = '\0';
    } else {
      size_t w = 0;
      for (uint32_t i = 0; i < n && w + 4 < size; ++i) {
        w += xml::encode_utf8(charset::code_point(charset::Encoding::Windows1252, cncx_[at + i]), out + w);
      }
      out[w] = '\0';
    }
  }

 private:
  struct Tag {
    uint8_t tag, per, mask, end;
  };

  void tag_map(uint32_t at, uint32_t end, TagValues& tv) {
    struct Found {
      uint8_t tag;
      uint8_t per;
      uint32_t count;  // values, or kNone when `bytes` says how many bytes of them
      uint32_t bytes;
    } found[64];
    int nf = 0;
    uint32_t cb = 0;
    uint32_t data = at + control_bytes_;
    for (int t = 0; t < tag_count_; ++t) {
      const Tag& tg = tags_[t];
      if (tg.end == 1) {
        ++cb;
        continue;
      }
      if (at + cb >= end) break;
      uint32_t value = buf_[at + cb] & tg.mask;
      if (value == 0) continue;
      if (value == tg.mask) {
        int bits = 0;
        for (uint32_t m = tg.mask; m != 0; m >>= 1) bits += m & 1;
        if (bits > 1) {
          const uint32_t bytes = varint(buf_, end, data);
          found[nf++] = {tg.tag, tg.per, kNone, bytes};
        } else {
          found[nf++] = {tg.tag, tg.per, 1, 0};
        }
      } else {
        uint32_t mask = tg.mask;
        while ((mask & 1) == 0) {
          mask >>= 1;
          value >>= 1;
        }
        found[nf++] = {tg.tag, tg.per, value, 0};
      }
    }
    for (int i = 0; i < nf; ++i) {
      const Found& f = found[i];
      uint8_t k = 0;
      auto put = [&](uint32_t v) {
        if (f.tag < kMaxTags && k < 4) tv.v[f.tag][k++] = v;
      };
      if (f.count != kNone) {
        for (uint32_t c = 0; c < f.count * f.per && data < end; ++c) put(varint(buf_, end, data));
      } else {
        const uint32_t stop = data + f.bytes;
        while (data < stop && data < end) put(varint(buf_, end, data));
      }
      if (f.tag < kMaxTags) tv.count[f.tag] = k;
    }
  }

  const Pdb& pdb_;
  uint8_t* buf_;
  uint8_t* cncx_;
  uint32_t first_ = 0;
  uint32_t records_ = 0;
  bool utf8_ = true;
  uint32_t control_bytes_ = 0;
  Tag tags_[64];
  int tag_count_ = 0;
  uint32_t cncx_loaded_ = kNone;
  int32_t cncx_len_ = -1;
};

// ---- The book --------------------------------------------------------------------

class Mobi {
 public:
  explicit Mobi(Job& job) : job_(job), pdb_(*job.in, reinterpret_cast<uint32_t*>(job.work)) {
    // Work memory, in order.
    uint8_t* p = job.work + (kMaxRecords * 4 + 4 + 15) / 16 * 16;
    auto take = [&](uint32_t n) {
      uint8_t* at = p;
      p += (n + 15) / 16 * 16;
      return at;
    };
    record_ = take(kRecordBytes);
    text_ = take(kTextBytes);
    cncx_ = take(kRecordBytes);
    marks_ = reinterpret_cast<Mark*>(take(kMaxMarks * sizeof(Mark)));
    image_ext_ = take(kMaxImages);
    picture_ = take(kImageBytes);  // pictures, and index records while they are read
    // Everything after: the rewriter, then the Huffman dictionaries or KF8's tables and file.
    rest_ = p;
    rest_size_ = static_cast<uint32_t>(job.work + job.work_size - p);
  }

  Result run() {
    if (!pdb_.open()) return Result::Unreadable;
    const bool palmdoc_only = pdb_.is("TEXtREAd");
    if (!palmdoc_only && !pdb_.is("BOOKMOBI")) return Result::Unreadable;
    if (!read_header(0)) return Result::Unreadable;
    if (encryption_ != 0) return Result::Protected;
    std::memset(image_ext_, 0, kMaxImages);
    if (palmdoc_only || !mobi_header_) return plain_text();
    // A KF8 book, or a joint one with KF8 after the older text: the older
    // text is read (its contents index is simpler), unless it is all there is.
    if (version_ >= 8) return kf8();
    return mobi6();
  }

 private:
  // Record 0 of the part starting at record `base`.
  bool read_header(uint32_t base) {
    base_ = base;
    const int32_t n = pdb_.read(base, record_, kRecordBytes);
    if (n < 16) return false;
    const uint8_t* h = record_;
    compression_ = be16(h);
    text_length_ = be32(h + 4);
    text_records_ = be16(h + 8);
    encryption_ = be16(h + 12);
    mobi_header_ = n >= 0x84 && std::memcmp(h + 16, "MOBI", 4) == 0;
    if (!mobi_header_) return compression_ == 1 || compression_ == 2;
    const uint32_t hlen = be32(h + 20);
    encoding_ = be32(h + 28);
    version_ = be32(h + 36);
    auto field = [&](uint32_t at) {
      return at + 4 <= 16 + hlen && at + 4 <= static_cast<uint32_t>(n) ? be32(h + at) : kNone;
    };
    first_image_ = field(0x6C);
    huff_record_ = field(0x70);
    huff_count_ = field(0x74);
    extra_flags_ = field(0xF0) == kNone ? 0 : field(0xF0) & 0xFFFF;
    ncx_ = field(0xF4);
    if (version_ >= 8) {
      fdst_ = field(0xC0);
      fragments_ = field(0xF8);
      skeletons_ = field(0xFC);
    }
    const uint32_t locale = field(0x5C);
    language_from_locale(locale == kNone ? 0 : locale & 0xFF);
    // The title: the full name, unless EXTH has a better one.
    const uint32_t name_at = field(0x54), name_len = field(0x58);
    if (name_at != kNone && name_len != kNone && name_at + name_len <= static_cast<uint32_t>(n)) {
      to_utf8(h + name_at, std::min<uint32_t>(name_len, 150), title_, sizeof(title_));
    }
    if ((field(0x80) & 0x40) != 0 && field(0x80) != kNone) read_exth(16 + hlen, static_cast<uint32_t>(n));
    return true;
  }

  void read_exth(uint32_t at, uint32_t n) {
    const uint8_t* h = record_;
    if (at + 12 > n || std::memcmp(h + at, "EXTH", 4) != 0) return;
    const uint32_t count = be32(h + at + 8);
    uint32_t p = at + 12;
    for (uint32_t i = 0; i < count && p + 8 <= n; ++i) {
      const uint32_t type = be32(h + p), len = be32(h + p + 4);
      if (len < 8 || p + len > n) break;
      const uint8_t* d = h + p + 8;
      const uint32_t dl = len - 8;
      if (type == 100 && author_[0] == '\0') to_utf8(d, std::min<uint32_t>(dl, 150), author_, sizeof(author_));
      if (type == 503) to_utf8(d, std::min<uint32_t>(dl, 150), title_, sizeof(title_));
      if (type == 524 && dl > 0) {
        char code[sizeof(language_)];
        size_t k = 0;
        while (k < dl && k + 1 < sizeof(code) && d[k] != '-' && d[k] != '_') {
          code[k] = ascii_lower(static_cast<char>(d[k]));
          ++k;
        }
        code[k] = '\0';
        // "und" and other non-languages leave the header's locale.
        if (k >= 2 && k <= 3 && std::strcmp(code, "und") != 0) std::memcpy(language_, code, k + 1);
      }
      if (type == 201 && dl >= 4) cover_ = be32(d);
      p += len;
    }
  }

  void language_from_locale(uint32_t id) {
    static constexpr struct {
      uint8_t id;
      char code[3];
    } kCodes[] = {{0x09, "en"}, {0x27, "lt"}, {0x07, "de"}, {0x0C, "fr"}, {0x0A, "es"}, {0x10, "it"},
                  {0x19, "ru"}, {0x26, "lv"}, {0x25, "et"}, {0x15, "pl"}, {0x13, "nl"}, {0x16, "pt"},
                  {0x1D, "sv"}, {0x14, "nb"}, {0x06, "da"}, {0x0B, "fi"}, {0x05, "cs"}, {0x22, "uk"}};
    for (const auto& c : kCodes) {
      if (c.id == id) std::memcpy(language_, c.code, 3);
    }
  }

  void to_utf8(const uint8_t* d, uint32_t len, char* out, size_t size) {
    const bool utf8 = encoding_ == 65001;
    size_t w = 0;
    for (uint32_t i = 0; i < len && w + 4 < size; ++i) {
      if (d[i] == 0) break;
      if (utf8) {
        out[w++] = static_cast<char>(d[i]);
      } else {
        w += xml::encode_utf8(charset::code_point(charset::Encoding::Windows1252, d[i]), out + w);
      }
    }
    // A character cut off at the end is dropped.
    size_t lead = w;
    while (lead > 0 && (static_cast<uint8_t>(out[lead - 1]) & 0xC0) == 0x80) --lead;
    if (lead > 0 && static_cast<uint8_t>(out[lead - 1]) >= 0xC0) {
      const auto c = static_cast<uint8_t>(out[lead - 1]);
      const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
      if (w - (lead - 1) < need) w = lead - 1;
    }
    out[w] = '\0';
  }

  // Text record i (from 1) of this part, decompressed into text_.
  int32_t text_record(uint32_t i) {
    const int32_t n = pdb_.read(base_ + i, record_, kRecordBytes);
    if (n < 0) return -1;
    const uint32_t len =
        mobi_header_ ? text_length(record_, static_cast<uint32_t>(n), extra_flags_) : static_cast<uint32_t>(n);
    switch (compression_) {
      case 1: {
        const uint32_t k = std::min(len, kTextBytes);
        std::memcpy(text_, record_, k);
        return static_cast<int32_t>(k);
      }
      case 2:
        return static_cast<int32_t>(palmdoc(record_, len, text_, kTextBytes));
      case 17480:
        return static_cast<int32_t>(huff_ok_ ? huff_.unpack(record_, len, text_, kTextBytes) : 0);
      default:
        return -1;
    }
  }

  bool prepare_huff() {
    if (compression_ != 17480) return true;
    if (huff_record_ == kNone || huff_count_ == kNone || huff_count_ < 2) return false;
    // The dictionaries go after the rewriter.
    constexpr uint32_t kPhrases = 65536;
    if (rest_size_ < kRewriterBytes + kPhrases * 4 + 64 * 1024) return false;
    auto* index = reinterpret_cast<uint32_t*>(rest_ + kRewriterBytes);
    uint8_t* pool = rest_ + kRewriterBytes + kPhrases * 4;
    const uint32_t pool_size = rest_size_ - kRewriterBytes - kPhrases * 4;
    huff_ok_ = huff_.load(pdb_, base_ + huff_record_, huff_count_, pool, pool_size, index, kPhrases);
    return huff_ok_;
  }

  // Writes picture n (counted from the first image record) once; its file
  // name, or nullptr when it is not a picture.
  const char* picture(uint32_t n) {
    static constexpr const char* kExt[] = {"", "jpg", "png", "gif", "bmp"};
    if (first_image_ == kNone || n >= kMaxImages) return nullptr;
    if (image_ext_[n] == 0) {
      image_ext_[n] = 255;
      const uint32_t rec = base_ + first_image_ + n;
      const uint32_t size = pdb_.size(rec);
      if (size > 0 && size <= kImageBytes && pdb_.read(rec, picture_, size) == static_cast<int32_t>(size)) {
        const char* ext = picture_ext(picture_, size);
        char path[160];
        if (ext != nullptr) {
          std::snprintf(path, sizeof(path), "%s%lu.%s", job_.pictures, static_cast<unsigned long>(n), ext);
          if (job_.storage->write(path, picture_, size)) {
            for (uint8_t k = 1; k < 5; ++k) {
              if (std::strcmp(kExt[k], ext) == 0) image_ext_[n] = k;
            }
          }
        }
      }
    }
    if (image_ext_[n] == 255) return nullptr;
    const char* base = std::strrchr(job_.pictures, '/');
    std::snprintf(name_, sizeof(name_), "%s%lu.%s", base != nullptr ? base + 1 : job_.pictures,
                  static_cast<unsigned long>(n), kExt[image_ext_[n]]);
    return name_;
  }

  // The contents, from the NCX index: (pos, depth, title) for MOBI; for KF8
  // pos is the fragment and file the offset in it, resolved later.
  int read_ncx(bool kf8) {
    if (ncx_ == kNone) return 0;
    int count = 0;
    Index index(pdb_, picture_, cncx_);
    index.read(base_ + ncx_, [&](std::string_view, const TagValues& tv) {
      if (count >= kMaxMarks) return;
      const uint32_t label = tv.get(3, 0);
      uint32_t pos = tv.get(1, 0), off = 0;
      if (kf8) {
        pos = tv.get(6, 0);
        off = tv.get(6, 1, 0);
      }
      if (pos == kNone || label == kNone) return;
      Mark& m = marks_[count];
      m.pos = pos;
      m.off = off;
      m.file = 0;
      const uint32_t depth = tv.get(4, 0, 0);
      m.depth = static_cast<uint8_t>(std::min<uint32_t>(depth, 8));
      index.label(label, m.title, sizeof(m.title));
      if (m.title[0] != '\0') ++count;
    });
    return count;
  }

  // ---- Rewriting the HTML ----

  class Rewriter {
   public:
    Rewriter(Mobi& book, Page& page, bool utf8, bool kf8) : book_(book), page_(page), utf8_(utf8), kf8_(kf8) {}

    // Marks (sorted by pos) to place before the tags at or past their pos.
    void set_marks(const Mark* marks, int count) {
      marks_ = marks;
      mark_count_ = count;
      next_mark_ = 0;
    }

    void feed(const uint8_t* d, uint32_t len, uint32_t pos) {
      for (uint32_t i = 0; i < len; ++i) {
        const uint8_t c = d[i];
        if (in_tag_) {
          if (tag_len_ < sizeof(tag_)) tag_[tag_len_++] = static_cast<char>(c);
          if (c == '>') {
            const std::string_view t(tag_, tag_len_);
            if (t.starts_with("<!--") && !t.ends_with("-->")) continue;
            in_tag_ = false;
            tag(t, tag_at_);
          }
          continue;
        }
        if (c == '<') {
          in_tag_ = true;
          tag_at_ = pos + i;
          tag_len_ = 0;
          tag_[tag_len_++] = '<';
          continue;
        }
        if (skip_ > 0) continue;
        if (c == '\0') continue;
        if (title_pending_ && !capturing_ && c != ' ' && c != '\n' && c != '\r' && c != '\t') title_pending_ = false;
        if (utf8_ || c < 0x80) {
          out(std::string_view(reinterpret_cast<const char*>(&c), 1));
        } else {
          char u[4];
          out(std::string_view(u, xml::encode_utf8(charset::code_point(charset::Encoding::Windows1252, c), u)));
        }
      }
    }

    // Writes out a paragraph still held back.
    void finish() {
      if (capturing_) release(false);
    }

   private:
    void place_marks(uint32_t pos) {
      while (next_mark_ < mark_count_ && marks_[next_mark_].pos <= pos) {
        if (capturing_) release(false);
        const Mark& m = marks_[next_mark_++];
        page_.chapter(m.title, m.depth);
        title_ = m.title;
        title_pending_ = !kf8_;
      }
    }

    void out(std::string_view s) {
      if (!capturing_) {
        page_.raw(s);
        return;
      }
      if (cap_len_ + s.size() > sizeof(cap_)) {
        release(false);
        page_.raw(s);
        return;
      }
      std::memcpy(cap_ + cap_len_, s.data(), s.size());
      cap_len_ += s.size();
    }

    // Old Kindle books set chapter titles as bold paragraphs, not headings:
    // the first paragraph after a contents entry, when it says the entry's
    // title, becomes a heading again.
    void release(bool closed) {
      capturing_ = false;
      const std::string_view inner(cap_, cap_len_);
      if (closed && same_words(inner, title_)) {
        page_.raw("<h2>");
        page_.raw(inner);
        page_.raw("</h2>");
        return;
      }
      page_.raw(std::string_view(open_, open_len_));
      page_.raw(inner);
      if (closed) page_.raw("</p>");
    }

    static bool same_words(std::string_view html, const char* title) {
      char text[512];
      size_t n = 0;
      bool in = false;
      for (size_t i = 0; i < html.size(); ++i) {
        const char c = html[i];
        if (c == '<') in = true;
        if (!in) {
          if (c == '&') {
            const size_t semi = html.find(';', i);
            if (semi == std::string_view::npos) return false;
            n = append_xml_text(html.substr(i, semi + 1 - i), text, n, sizeof(text));
            i = semi;
          } else if (n + 1 < sizeof(text)) {
            text[n++] = c;
          }
        }
        if (c == '>') in = false;
      }
      // Compared without case or spacing.
      size_t a = 0;
      const char* b = title;
      auto skip = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
      for (;;) {
        while (a < n && skip(text[a])) ++a;
        while (*b != '\0' && skip(*b)) ++b;
        if (a == n || *b == '\0') return a == n && *b == '\0' && b != title;
        if (ascii_lower(text[a]) != ascii_lower(*b)) return false;
        ++a;
        ++b;
      }
    }

    void tag(std::string_view t, uint32_t pos) {
      size_t p = 0, start = 0;
      xml::Tag tag;
      if (!xml::next_tag(t, p, tag, start)) return;  // a comment, or not a tag
      const std::string_view name = tag.name;
      if (xml::name_is(name, "head") || xml::name_is(name, "script") || xml::name_is(name, "style")) {
        if (!tag.self_closing) skip_ += tag.closing ? (skip_ > 0 ? -1 : 0) : 1;
        return;
      }
      if (skip_ > 0) return;
      place_marks(pos);
      if (capturing_) {
        if (xml::name_is(name, "p") && tag.closing) {
          release(true);
          return;
        }
        // Only words and emphasis can be a title.
        if (!(xml::name_is(name, "b") || xml::name_is(name, "i") || xml::name_is(name, "em") ||
              xml::name_is(name, "strong") || xml::name_is(name, "font") || xml::name_is(name, "span") ||
              xml::name_is(name, "big") || xml::name_is(name, "u") || xml::name_is(name, "a"))) {
          release(false);
        }
      } else if (title_pending_ && xml::name_is(name, "p") && !tag.closing && !tag.self_closing &&
                 t.size() <= sizeof(open_)) {
        title_pending_ = false;
        capturing_ = true;
        cap_len_ = 0;
        std::memcpy(open_, t.data(), t.size());
        open_len_ = t.size();
        return;
      } else if (title_pending_ && !xml::name_is(name, "div") && !xml::name_is(name, "a") &&
                 !xml::name_is(name, "font") && !xml::name_is(name, "pagebreak")) {
        title_pending_ = false;
      }
      if (xml::name_is(name, "html") || xml::name_is(name, "body") || xml::name_is(name, "guide") ||
          xml::name_is(name, "reference") || xml::name_is(name, "font") || xml::name_is(name, "link") ||
          xml::name_is(name, "meta") || xml::name_is(name, "title") || xml::name_is(name, "?xml") ||
          t.starts_with("<!") || t.starts_with("<?")) {
        return;
      }
      if (xml::name_is(name, "a")) {
        // Links inside the book point at byte positions (filepos) or KF8
        // fragments: the words stay, the link does not.
        if (tag.closing) return;
        const std::string_view id = xml::attr(tag.attrs, "id");
        if (!id.empty()) {
          out("<a id=\"");
          out(id);
          out("\"></a>");
        }
        return;
      }
      if (xml::name_is(name, "pagebreak")) {
        out("<mbp:pagebreak/>");
        return;
      }
      if (t.size() > 5 && (t.starts_with("<mbp:") || t.starts_with("</mbp:"))) return;
      if (!tag.closing && (xml::name_is(name, "img") || xml::name_is(name, "image"))) {
        const char* file = nullptr;
        if (!kf8_) {
          const std::string_view rec = xml::attr(tag.attrs, "recindex");
          uint32_t n = 0;
          for (char c : rec) {
            if (c >= '0' && c <= '9') n = n * 10 + static_cast<uint32_t>(c - '0');
          }
          if (n > 0) file = book_.picture(n - 1);
        } else {
          std::string_view src = xml::attr(tag.attrs, "src");
          if (src.empty()) src = xml::attr(tag.attrs, "href");
          // kindle:embed:XXXX, a number in base 32 counted from 1.
          if (src.starts_with("kindle:embed:")) {
            uint32_t n = 0;
            for (char c : src.substr(13, 4)) {
              const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'V' ? c - 'A' + 10 : -1;
              if (v < 0) break;
              n = n * 32 + static_cast<uint32_t>(v);
            }
            if (n > 0) file = book_.picture(n - 1);
          }
        }
        if (file == nullptr) return;
        out("<img src=\"");
        out(file);
        out("\" alt=\"");
        out(xml::attr(tag.attrs, "alt"));
        out("\"/>");
        return;
      }
      // Anything else as it is: in the old format in the book's code page.
      if (utf8_) {
        out(t);
      } else {
        for (char c : t) {
          const auto b = static_cast<uint8_t>(c);
          char u[4];
          out(b < 0x80
                  ? std::string_view(&c, 1)
                  : std::string_view(u, xml::encode_utf8(charset::code_point(charset::Encoding::Windows1252, b), u)));
        }
      }
    }

    Mobi& book_;
    Page& page_;
    bool utf8_;
    bool kf8_;
    bool in_tag_ = false;
    char tag_[4096];
    size_t tag_len_ = 0;
    uint32_t tag_at_ = 0;
    int skip_ = 0;
    const Mark* marks_ = nullptr;
    int mark_count_ = 0;
    int next_mark_ = 0;
    const char* title_ = "";
    bool title_pending_ = false;
    bool capturing_ = false;
    char open_[256];
    size_t open_len_ = 0;
    char cap_[512];
    size_t cap_len_ = 0;
  };
  static_assert(sizeof(Rewriter) <= kRewriterBytes);

  // Writes the head; the cover is written first so the head can name it.
  void start(Page& page, bool marks) {
    const char* cover = cover_ != kNone ? picture(cover_) : nullptr;
    char cover_name[96] = "";
    if (cover != nullptr) std::snprintf(cover_name, sizeof(cover_name), "%s", cover);
    if (title_[0] == '\0') to_utf8(reinterpret_cast<const uint8_t*>(pdb_.name()), 32, title_, sizeof(title_));
    page.start(title_, author_, language_, cover_name, marks);
  }

  Result mobi6() {
    if (!prepare_huff()) return Result::Unreadable;
    const int marks = read_ncx(false);
    std::sort(marks_, marks_ + marks, [](const Mark& a, const Mark& b) { return a.pos < b.pos; });
    Page page(job_);
    if (!page.ok()) return Result::Unreadable;
    start(page, marks > 0);
    auto* rw = new (rest_) Rewriter(*this, page, encoding_ == 65001, false);
    rw->set_marks(marks_, marks);
    uint32_t pos = 0;
    bool ok = true;
    for (uint32_t i = 1; i <= text_records_ && pos < text_length_; ++i) {
      const int32_t n = text_record(i);
      if (n < 0) {
        ok = false;
        break;
      }
      const uint32_t len = std::min(static_cast<uint32_t>(n), text_length_ - pos);
      rw->feed(text_, len, pos);
      pos += len;
    }
    rw->finish();
    rw->~Rewriter();
    page.end();
    if (!ok || pos == 0) return Result::Unreadable;
    return page.finish(true) ? Result::Ok : Result::Unreadable;
  }

  // A PalmDOC book: plain text, a paragraph per line.
  Result plain_text() {
    if (compression_ != 1 && compression_ != 2) return Result::Unreadable;
    encoding_ = 1252;
    Page page(job_);
    if (!page.ok()) return Result::Unreadable;
    to_utf8(reinterpret_cast<const uint8_t*>(pdb_.name()), 32, title_, sizeof(title_));
    page.start(title_, "", "", "", false);
    // UTF-8 or a code page: decided from the first record.
    bool utf8 = true;
    if (text_record(1) > 0) {
      uint32_t bom = 0;
      utf8 = charset::detect(text_, std::min<uint32_t>(kTextBytes, 4096), bom) == charset::Encoding::Utf8;
    }
    bool open = false;
    uint32_t total = 0;
    for (uint32_t i = 1; i <= text_records_; ++i) {
      const int32_t n = text_record(i);
      if (n < 0) return Result::Unreadable;
      for (int32_t k = 0; k < n; ++k) {
        const uint8_t c = text_[k];
        if (c == '\n' || c == '\r') {
          if (open) page.raw("</p>\n");
          open = false;
          continue;
        }
        if (!open) {
          if (c == ' ' || c == '\t') continue;
          page.raw("<p>");
          open = true;
        }
        if (utf8 || c < 0x80) {
          if (c == '&' || c == '<' || c == '>') {
            page.code_point(c);
          } else {
            page.raw(static_cast<char>(c));
          }
        } else {
          page.code_point(charset::code_point(charset::Encoding::Windows1252, c));
        }
      }
      total += static_cast<uint32_t>(n);
    }
    if (open) page.raw("</p>\n");
    page.end();
    if (total == 0) return Result::NoText;
    return page.finish(true) ? Result::Ok : Result::Unreadable;
  }

  Result kf8() {
    if (!prepare_huff()) return Result::Unreadable;
    // The HTML flow ends where the FDST record says; CSS and SVG come after.
    uint32_t flow_end = text_length_;
    if (fdst_ != kNone) {
      const int32_t n = pdb_.read(base_ + fdst_, record_, kRecordBytes);
      if (n >= 20 && std::memcmp(record_, "FDST", 4) == 0 && be32(record_ + 8) > 0) {
        flow_end = std::min(flow_end, be32(record_ + 16));
      }
    }
    // The text, unpacked to the card: files are put together from pieces
    // anywhere in it.
    Storage& storage = *job_.storage;
    Writer* w = storage.create(job_.scratch_path);
    if (w == nullptr) return Result::Unreadable;
    uint32_t pos = 0;
    bool ok = true;
    for (uint32_t i = 1; i <= text_records_ && pos < flow_end && ok; ++i) {
      const int32_t n = text_record(i);
      if (n < 0) {
        ok = false;
        break;
      }
      const uint32_t len = std::min(static_cast<uint32_t>(n), flow_end - pos);
      ok = w->write(text_, len);
      pos += len;
    }
    if (!storage.finish(w, ok) || !ok) return Result::Unreadable;
    File* raw = storage.open(job_.scratch_path);
    if (raw == nullptr) return Result::Unreadable;
    const Result r = kf8_files(*raw, pos);
    storage.close(raw);
    storage.remove(job_.scratch_path);
    return r;
  }

  struct Frag {
    uint32_t insert;  // where in its file, counted from the skeleton's start in the text
    uint32_t file;
    uint32_t length;
  };
  struct Skel {
    uint32_t start;
    uint32_t length;
    uint32_t frags;
  };

  Result kf8_files(File& raw, uint32_t raw_size) {
    // Memory: the rewriter, the tables, then the file being put together.
    auto* skels = reinterpret_cast<Skel*>(rest_ + kRewriterBytes);
    auto* frags = reinterpret_cast<Frag*>(skels + kMaxFiles);
    uint8_t* file_buf = reinterpret_cast<uint8_t*>(frags + kMaxFrags);
    const uint32_t used = static_cast<uint32_t>(file_buf - rest_);
    if (rest_size_ < used + 128 * 1024) return Result::NoMemory;
    const uint32_t file_cap = rest_size_ - used;
    uint8_t* index_buf = picture_;  // free until pictures are written

    uint32_t skel_count = 0, frag_count = 0;
    if (skeletons_ != kNone && fragments_ != kNone) {
      Index skel_index(pdb_, index_buf, cncx_);
      skel_index.read(base_ + skeletons_, [&](std::string_view, const TagValues& tv) {
        if (skel_count >= kMaxFiles) return;
        const uint32_t start = tv.get(6, 0), len = tv.get(6, 1);
        if (start == kNone || len == kNone) return;
        skels[skel_count++] = {start, len, tv.get(1, 0, 0)};
      });
      Index frag_index(pdb_, index_buf, cncx_);
      frag_index.read(base_ + fragments_, [&](std::string_view text, const TagValues& tv) {
        if (frag_count >= kMaxFrags) return;
        uint32_t insert = 0;
        for (char c : text) {
          if (c >= '0' && c <= '9') insert = insert * 10 + static_cast<uint32_t>(c - '0');
        }
        const uint32_t len = tv.get(6, 1);
        if (len == kNone) return;
        frags[frag_count++] = {insert, tv.get(3, 0, 0), len};
      });
    }
    // The contents: each entry names a fragment and an offset from its start.
    const int marks = read_ncx(true);
    for (int k = 0; k < marks; ++k) {
      Mark& m = marks_[k];
      const uint32_t fid = m.pos;
      if (fid < frag_count && frags[fid].file < skel_count) {
        m.file = static_cast<uint16_t>(frags[fid].file);
        m.pos = frags[fid].insert - skels[frags[fid].file].start + m.off;
      } else {
        m.file = 0xFFFF;  // nowhere
      }
    }
    std::sort(marks_, marks_ + marks,
              [](const Mark& a, const Mark& b) { return a.file != b.file ? a.file < b.file : a.pos < b.pos; });

    Page page(job_);
    if (!page.ok()) return Result::Unreadable;
    start(page, marks > 0);
    auto* rw = new (rest_) Rewriter(*this, page, true, true);
    bool ok = true;
    if (skel_count == 0) {
      // No skeletons: the text as it is.
      rw->set_marks(nullptr, 0);
      for (uint32_t at = 0; at < raw_size && ok;) {
        const uint32_t n = std::min(raw_size - at, file_cap);
        ok = raw.read_at(at, file_buf, n) == static_cast<int32_t>(n);
        if (ok) rw->feed(file_buf, n, at);
        at += n;
      }
    } else {
      uint32_t next_frag = 0;
      int mark = 0;
      for (uint32_t s = 0; s < skel_count && ok; ++s) {
        const Skel& sk = skels[s];
        // The skeleton, then each fragment inserted where it belongs.
        uint32_t len = std::min(sk.length, file_cap);
        ok = sk.start + len <= raw_size && raw.read_at(sk.start, file_buf, len) == static_cast<int32_t>(len);
        uint32_t from = sk.start + sk.length;
        for (uint32_t k = 0; k < sk.frags && next_frag < frag_count && ok; ++k, ++next_frag) {
          const Frag& fr = frags[next_frag];
          const uint32_t at = fr.insert >= sk.start ? fr.insert - sk.start : kNone;
          if (at > len || len + fr.length > file_cap || from + fr.length > raw_size) {
            from += fr.length;
            continue;  // out of place, or too much: left out
          }
          std::memmove(file_buf + at + fr.length, file_buf + at, len - at);
          ok = raw.read_at(from, file_buf + at, fr.length) == static_cast<int32_t>(fr.length);
          len += fr.length;
          from += fr.length;
        }
        if (!ok) break;
        while (mark < marks && marks_[mark].file < s) ++mark;
        const int first = mark;
        while (mark < marks && marks_[mark].file == s) ++mark;
        rw->set_marks(marks_ + first, mark - first);
        if (s > 0) page.raw("<mbp:pagebreak/>\n");
        rw->feed(file_buf, len, 0);
      }
    }
    rw->finish();
    rw->~Rewriter();
    page.end();
    if (!ok) return Result::Unreadable;
    return page.finish(true) ? Result::Ok : Result::Unreadable;
  }

  Job& job_;
  Pdb pdb_;
  uint8_t* record_;
  uint8_t* text_;
  uint8_t* cncx_;
  Mark* marks_;
  uint8_t* image_ext_;
  uint8_t* rest_;
  uint32_t rest_size_;
  uint8_t* picture_ = nullptr;
  Huff huff_;
  bool huff_ok_ = false;

  uint32_t base_ = 0;
  uint32_t compression_ = 0;
  uint32_t text_length_ = 0;
  uint32_t text_records_ = 0;
  uint32_t encryption_ = 0;
  bool mobi_header_ = false;
  uint32_t encoding_ = 1252;
  uint32_t version_ = 0;
  uint32_t first_image_ = kNone;
  uint32_t huff_record_ = kNone;
  uint32_t huff_count_ = kNone;
  uint32_t extra_flags_ = 0;
  uint32_t ncx_ = kNone;
  uint32_t fdst_ = kNone;
  uint32_t fragments_ = kNone;
  uint32_t skeletons_ = kNone;
  uint32_t cover_ = kNone;
  char title_[160] = "";
  char author_[160] = "";
  char language_[16] = "";
  char name_[96] = "";
};

}  // namespace

Result mobi(Job& job) {
  static_assert(kMaxRecords * 4 + 4 + 2 * kRecordBytes + kTextBytes + kMaxMarks * sizeof(Mark) + kMaxImages +
                    kImageBytes + kRewriterBytes + kMaxFiles * 12 + kMaxFrags * 12 + 256 * 1024 <
                kWorkBytes);
  auto* book = new (std::nothrow) Mobi(job);
  if (book == nullptr) return Result::NoMemory;
  const Result r = book->run();
  delete book;
  return r;
}

}  // namespace vellum::convert

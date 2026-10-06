#include "vellum/covers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/bmp.h"
#include "vellum/ui.h"

extern "C" {
#include "tjpgd.h"
}

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vellum::covers {
namespace {

constexpr char kMagic[4] = {'V', 'C', 'V', '1'};
constexpr size_t kHeader = 12;  // magic, then large w, h and small w, h as 16-bit little-endian
constexpr size_t kPoolBytes = 8 * 1024;
constexpr uint32_t kMaxPngPixels = 2'000'000;  // a full-size decode; larger PNGs keep the drawn cover
constexpr uint32_t kMaxPngFile = 1024 * 1024;  // a PNG picture file is read whole first

// Shrinks a picture into a box of at most max_w x max_h, keeping its shape.
void fit(int w, int h, int max_w, int max_h, int& out_w, int& out_h) {
  if (int64_t{w} * max_h > int64_t{h} * max_w) {
    out_w = std::min(w, max_w);
    out_h = std::max(1, static_cast<int>(int64_t{h} * out_w / w));
  } else {
    out_h = std::min(h, max_h);
    out_w = std::max(1, static_cast<int>(int64_t{w} * out_h / h));
  }
}

// Area-averages source pixels into a thumbnail as they arrive, in any order.
// Every source pixel arrives once, so how many fall on a thumbnail pixel is
// its column's count times its row's: no count per pixel is kept.
struct Shrinker {
  int src_w = 0, src_h = 0;  // the (possibly descaled) decoded size
  int w = 0, h = 0;          // the thumbnail
  uint32_t* sum = nullptr;   // per thumbnail pixel

  void start() const { std::fill_n(sum, w * h, 0u); }
  void add(int x, int y, uint8_t v) const {
    const int tx = static_cast<int>(int64_t{x} * w / src_w);
    const int ty = static_cast<int>(int64_t{y} * h / src_h);
    sum[ty * w + tx] += v;
  }
  void finish(uint8_t* out) const {
    static uint32_t cols[kLargeW], rows[kLargeH];
    std::fill_n(cols, w, 0u);
    std::fill_n(rows, h, 0u);
    for (int x = 0; x < src_w; ++x) ++cols[static_cast<int>(int64_t{x} * w / src_w)];
    for (int y = 0; y < src_h; ++y) ++rows[static_cast<int>(int64_t{y} * h / src_h)];
    for (int ty = 0; ty < h; ++ty) {
      for (int tx = 0; tx < w; ++tx) {
        const uint32_t n = cols[tx] * rows[ty];
        out[ty * w + tx] = n ? static_cast<uint8_t>(sum[ty * w + tx] / n) : 255;
      }
    }
  }
};

struct Work {
  uint8_t* pool;
  uint32_t* sum;
  uint8_t* large;
  uint8_t* small;
  bool ok() const { return pool && sum && large && small; }
};

Work& work() {
  static Work w{new (std::nothrow) uint8_t[kPoolBytes], new (std::nothrow) uint32_t[kLargeW * kLargeH],
                new (std::nothrow) uint8_t[kLargeW * kLargeH], new (std::nothrow) uint8_t[kSmallW * kSmallH]};
  return w;
}

struct JpegInput {
  const uint8_t* data;
  uint32_t len;
  uint32_t pos;
  Shrinker* shrink;
  File* file = nullptr;  // read from here instead of data
};

size_t jpeg_in(JDEC* jd, uint8_t* buf, size_t n) {
  auto* in = static_cast<JpegInput*>(jd->device);
  n = std::min<size_t>(n, in->len - in->pos);
  if (buf != nullptr && in->file != nullptr) {
    // A picture file, read as the decoder goes.
    if (in->file->read_at(in->pos, buf, static_cast<uint32_t>(n)) != static_cast<int32_t>(n)) return 0;
  } else if (buf != nullptr) {
    std::memcpy(buf, in->data + in->pos, n);
  }
  in->pos += static_cast<uint32_t>(n);
  return n;
}

int jpeg_out(JDEC* jd, void* bitmap, JRECT* r) {
  auto* in = static_cast<JpegInput*>(jd->device);
  const auto* px = static_cast<const uint8_t*>(bitmap);
  for (int y = r->top; y <= r->bottom; ++y) {
    for (int x = r->left; x <= r->right; ++x) {
      if (x < in->shrink->src_w && y < in->shrink->src_h) in->shrink->add(x, y, *px);
      ++px;
    }
  }
  return 1;
}

// Decodes into `out`, kLargeW x kLargeH at most (wk.large unless given);
// sets its size. A JPEG may come from `file` (`data` unused, `len` its size).
bool decode_large(const uint8_t* data, uint32_t len, Work& wk, int& w, int& h, uint8_t* out = nullptr,
                  File* file = nullptr) {
  Shrinker s;
  s.sum = wk.sum;
  const bool png = file == nullptr && len >= 8 && std::memcmp(data, "\x89PNG", 4) == 0;
  if (!png) {
    JDEC jd;
    JpegInput in{data, len, 0, &s, file};
    if (jd_prepare(&jd, jpeg_in, wk.pool, kPoolBytes, &in) != JDR_OK) return false;
    fit(jd.width, jd.height, kLargeW, kLargeH, w, h);
    // Let the decoder shrink as far as it can while staying above the thumbnail.
    uint8_t scale = 0;
    while (scale < 3 && (jd.width >> (scale + 1)) >= w && (jd.height >> (scale + 1)) >= h) ++scale;
    s.src_w = (jd.width + (1 << scale) - 1) >> scale;
    s.src_h = (jd.height + (1 << scale) - 1) >> scale;
    s.w = w;
    s.h = h;
    s.start();
    if (jd_decomp(&jd, jpeg_out, scale) != JDR_OK) return false;
  } else {
    int pw, ph, comp;
    if (!stbi_info_from_memory(data, static_cast<int>(len), &pw, &ph, &comp)) return false;
    if (uint64_t{static_cast<uint32_t>(pw)} * static_cast<uint32_t>(ph) > kMaxPngPixels) return false;
    uint8_t* grey = stbi_load_from_memory(data, static_cast<int>(len), &pw, &ph, &comp, 1);
    if (grey == nullptr) return false;
    fit(pw, ph, kLargeW, kLargeH, w, h);
    s.src_w = pw;
    s.src_h = ph;
    s.w = w;
    s.h = h;
    s.start();
    for (int y = 0; y < ph; ++y) {
      for (int x = 0; x < pw; ++x) s.add(x, y, grey[y * pw + x]);
    }
    stbi_image_free(grey);
  }
  s.finish(out != nullptr ? out : wk.large);
  return true;
}

// A BMP, row by row through the shrinker.
bool decode_bmp(File& file, Work& wk, uint8_t* out, int& w, int& h) {
  struct Ctx {
    Shrinker s;
    int* w;
    int* h;
  } ctx{{}, &w, &h};
  ctx.s.sum = wk.sum;
  bmp::Sink sink;
  sink.ctx = &ctx;
  sink.size = [](void* p, int pw, int ph) {
    auto* c = static_cast<Ctx*>(p);
    fit(pw, ph, kLargeW, kLargeH, *c->w, *c->h);
    c->s.src_w = pw;
    c->s.src_h = ph;
    c->s.w = *c->w;
    c->s.h = *c->h;
    c->s.start();
    return true;
  };
  sink.row = [](void* p, int y, const uint8_t* grey) {
    auto* c = static_cast<Ctx*>(p);
    for (int x = 0; x < c->s.src_w; ++x) c->s.add(x, y, grey[x]);
  };
  if (!bmp::read(file, sink)) return false;
  ctx.s.finish(out);
  return true;
}

void put16(uint8_t* p, int v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}
int get16(const uint8_t* p) { return p[0] | (p[1] << 8); }

}  // namespace

void path_for(const char* book_path, uint32_t file_size, char* out, size_t size) {
  uint32_t h = 2166136261u;  // FNV-1a over the path and size
  for (const char* p = book_path; *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  for (int i = 0; i < 4; ++i) h = (h ^ ((file_size >> (8 * i)) & 0xFF)) * 16777619u;
  std::snprintf(out, size, "%s/%08lx.cov", kFolder, static_cast<unsigned long>(h));
}

bool make(Storage& storage, const char* cover_path, const uint8_t* data, uint32_t len) {
  Work& wk = work();
  int lw, lh;
  if (!wk.ok() || len == 0 || !decode_large(data, len, wk, lw, lh)) return false;

  // The small thumbnail, from the large one.
  int sw, sh;
  fit(lw, lh, kSmallW, kSmallH, sw, sh);
  const Shrinker s{lw, lh, sw, sh, wk.sum};
  s.start();
  for (int y = 0; y < lh; ++y) {
    for (int x = 0; x < lw; ++x) s.add(x, y, wk.large[y * lw + x]);
  }
  s.finish(wk.small);

  // One file: header, large, small. The sums buffer is free again for it.
  auto* out = reinterpret_cast<uint8_t*>(wk.sum);
  std::memcpy(out, kMagic, 4);
  put16(out + 4, lw);
  put16(out + 6, lh);
  put16(out + 8, sw);
  put16(out + 10, sh);
  std::memcpy(out + kHeader, wk.large, static_cast<size_t>(lw) * lh);
  std::memcpy(out + kHeader + lw * lh, wk.small, static_cast<size_t>(sw) * sh);
  return storage.write(cover_path, out, static_cast<uint32_t>(kHeader + lw * lh + sw * sh));
}

bool load(Storage& storage, const char* cover_path, bool large, CoverImage& out) {
  File* f = storage.open(cover_path);
  if (f == nullptr) return false;
  uint8_t head[kHeader];
  bool ok = f->read_at(0, head, kHeader) == static_cast<int32_t>(kHeader) && std::memcmp(head, kMagic, 4) == 0;
  if (ok) {
    const int lw = get16(head + 4), lh = get16(head + 6), sw = get16(head + 8), sh = get16(head + 10);
    ok = lw > 0 && lh > 0 && lw <= kLargeW && lh <= kLargeH && sw > 0 && sh > 0 && sw <= kSmallW && sh <= kSmallH;
    if (ok) {
      out.w = large ? lw : sw;
      out.h = large ? lh : sh;
      const uint32_t at = static_cast<uint32_t>(kHeader + (large ? 0 : lw * lh));
      const uint32_t n = static_cast<uint32_t>(out.w * out.h);
      ok = f->read_at(at, out.px, n) == static_cast<int32_t>(n);
    }
  }
  storage.close(f);
  if (!ok) out.w = out.h = 0;
  return ok;
}

// ---- Pictures inside books ----------------------------------------------------

namespace {

constexpr char kPictureMagic[4] = {'V', 'P', 'C', '1'};
constexpr size_t kPictureHeader = 8;  // magic, then w and h as 16-bit little-endian

uint32_t row_bytes(int w) { return static_cast<uint32_t>((w + 7) / 8); }

}  // namespace

int picture_size(const uint8_t* d, uint32_t len, int& w, int& h) {
  w = h = 0;
  if (len < 4) return -1;
  if (std::memcmp(d, "\x89PNG", 4) == 0) {
    if (len < 24) return -1;
    const uint32_t pw = (uint32_t{d[16]} << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
    const uint32_t ph = (uint32_t{d[20]} << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
    if (pw == 0 || ph == 0 || pw > 0xFFFF || ph > 0xFFFF) return 0;
    w = static_cast<int>(pw);
    h = static_cast<int>(ph);
    return uint64_t{pw} * ph <= kMaxPngPixels ? 1 : 0;
  }
  if (d[0] != 0xFF || d[1] != 0xD8) return 0;
  // JPEG: segments up to the frame header, which has the size.
  uint32_t i = 2;
  for (;;) {
    while (i < len && d[i] != 0xFF) ++i;
    while (i < len && d[i] == 0xFF) ++i;
    if (i >= len) return -1;
    const uint8_t m = d[i++];
    if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;  // no length
    if (m == 0xD9 || m == 0xDA) return 0;                              // no frame before the data
    if (i + 2 > len) return -1;
    const uint32_t seg = (uint32_t{d[i]} << 8) | d[i + 1];
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      if (i + 7 > len) return -1;
      h = (d[i + 3] << 8) | d[i + 4];
      w = (d[i + 5] << 8) | d[i + 6];
      return m == 0xC0 && w > 0 && h > 0 ? 1 : 0;  // the decoder reads baseline JPEGs only
    }
    if (seg < 2) return 0;
    i += seg;
  }
}

void picture_path(uint32_t book_key, int entry, int w, int h, char* out, size_t size) {
  std::snprintf(out, size, "%s/%08lx-%d-%dx%d.pic", kPictureFolder, static_cast<unsigned long>(book_key), entry, w, h);
}

bool draw_kept(Storage& storage, const char* path, Framebuffer& fb, Rect r, uint8_t* scratch, uint32_t scratch_size) {
  const uint32_t stride = row_bytes(r.w);
  const uint32_t bytes = stride * static_cast<uint32_t>(r.h);
  if (r.empty() || bytes > scratch_size) return false;
  File* f = storage.open(path);
  if (f == nullptr) return false;
  uint8_t head[kPictureHeader];
  const bool ok = f->read_at(0, head, kPictureHeader) == static_cast<int32_t>(kPictureHeader) &&
                  std::memcmp(head, kPictureMagic, 4) == 0 && get16(head + 4) == r.w && get16(head + 6) == r.h &&
                  f->read_at(kPictureHeader, scratch, bytes) == static_cast<int32_t>(bytes);
  storage.close(f);
  if (!ok) return false;
  for (int y = 0; y < r.h; ++y) {
    const uint8_t* row = scratch + static_cast<size_t>(y) * stride;
    for (int x = 0; x < r.w; ++x) {
      const bool white = (row[x >> 3] & (0x80 >> (x & 7))) != 0;
      fb.set_pixel(r.x + x, r.y + y, white ? Color::White : Color::Black);
    }
  }
  return true;
}

bool draw_new(Storage* storage, const char* path, Framebuffer& fb, Rect r, const uint8_t* data, uint32_t len,
              uint8_t* scratch, uint32_t scratch_size) {
  Work& wk = work();
  int w, h;
  if (!wk.ok() || len == 0 || r.empty() || !decode_large(data, len, wk, w, h)) return false;
  const CoverImage picture{w, h, wk.large};
  ui::draw_picture(fb, r, picture);
  // Keep it as drawn: one bit a pixel, rows top to bottom. The data was
  // decoded and its buffer is free again.
  const uint32_t stride = row_bytes(r.w);
  const uint32_t bytes = stride * static_cast<uint32_t>(r.h);
  if (storage == nullptr || kPictureHeader + bytes > scratch_size) return true;
  std::memcpy(scratch, kPictureMagic, 4);
  put16(scratch + 4, r.w);
  put16(scratch + 6, r.h);
  uint8_t* bits = scratch + kPictureHeader;
  std::memset(bits, 0, bytes);
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w; ++x) {
      if (fb.pixel(r.x + x, r.y + y) == Color::White) bits[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    }
  }
  storage->write(path, scratch, static_cast<uint32_t>(kPictureHeader + bytes));
  return true;
}

bool load_picture(Storage& storage, const char* path, CoverImage& out) {
  Work& wk = work();
  out.w = out.h = 0;
  File* f = storage.open(path);
  if (f == nullptr) return false;
  uint8_t head[8] = {};
  const uint32_t size = f->size();
  const bool read = wk.ok() && f->read_at(0, head, sizeof(head)) == static_cast<int32_t>(sizeof(head));
  int w = 0, h = 0;
  bool ok = false;
  if (read && head[0] == 'B' && head[1] == 'M') {
    ok = decode_bmp(*f, wk, out.px, w, h);
  } else if (read && head[0] == 0xFF && head[1] == 0xD8) {
    ok = decode_large(nullptr, size, wk, w, h, out.px, f);  // read as it decodes: any file size
  } else if (read && std::memcmp(head, "\x89PNG", 4) == 0 && size <= kMaxPngFile) {
    // A PNG is decoded from memory: the file for a moment, then its pixels.
    auto* data = new (std::nothrow) uint8_t[size];
    ok = data != nullptr && f->read_at(0, data, size) == static_cast<int32_t>(size) &&
         decode_large(data, size, wk, w, h, out.px);
    delete[] data;
  }
  storage.close(f);
  if (ok) {
    out.w = w;
    out.h = h;
  }
  return ok;
}

}  // namespace vellum::covers

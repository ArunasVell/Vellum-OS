#include "vellum/covers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/bmp.h"

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
};

size_t jpeg_in(JDEC* jd, uint8_t* buf, size_t n) {
  auto* in = static_cast<JpegInput*>(jd->device);
  n = std::min<size_t>(n, in->len - in->pos);
  if (buf != nullptr) std::memcpy(buf, in->data + in->pos, n);
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

// Decodes into the large thumbnail; sets its size.
bool decode_large(const uint8_t* data, uint32_t len, Work& wk, int& w, int& h) {
  Shrinker s;
  s.sum = wk.sum;
  const bool png = len >= 8 && std::memcmp(data, "\x89PNG", 4) == 0;
  if (!png) {
    JDEC jd;
    JpegInput in{data, len, 0, &s};
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
  s.finish(wk.large);
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

// ---- Full-screen pictures ----------------------------------------------------

namespace {

constexpr char kScreenMagic[4] = {'V', 'S', 'P', '1'};

// The middle of a w x h picture that has the screen's shape.
void screen_crop(int w, int h, int& cx, int& cy, int& cw, int& ch) {
  cw = w;
  ch = h;
  if (int64_t{w} * kScreenH > int64_t{h} * kScreenW) {
    cw = std::max(1, static_cast<int>(int64_t{h} * kScreenW / kScreenH));
  } else {
    ch = std::max(1, static_cast<int>(int64_t{w} * kScreenH / kScreenW));
  }
  cx = (w - cw) / 2;
  cy = (h - ch) / 2;
}

// The size a w x h picture is kept at: the screen, or its crop when smaller.
void screen_size(int w, int h, int& out_w, int& out_h) {
  int cx, cy, cw, ch;
  screen_crop(w, h, cx, cy, cw, ch);
  if (cw >= kScreenW) {
    out_w = kScreenW;
    out_h = kScreenH;
  } else {
    out_w = cw;
    out_h = std::max(1, std::min(ch, static_cast<int>(int64_t{cw} * kScreenH / kScreenW)));
  }
}

// Like Shrinker, but for a picture as large as the screen: it cuts the
// picture to the screen's shape and keeps sums for a band of output rows
// only, finishing each row once the source has moved on past it. Source
// pixels must arrive roughly top to bottom (a JPEG's rows of blocks, or
// whole rows), never more than kBand output rows back.
struct ScreenShrinker {
  static constexpr int kBand = 32;
  int cx = 0, cy = 0, cw = 1, ch = 1;  // the crop, in source pixels
  int w = 1, h = 1;                    // the picture kept
  uint32_t* band = nullptr;            // kBand rows of w sums
  uint32_t* cols = nullptr;            // source pixels per output column (kScreenW)
  uint32_t* rows = nullptr;            // and per output row (kScreenH)
  uint8_t* out = nullptr;              // w * h
  int done = 0;                        // output rows finished

  void start(int src_w, int src_h, int want_w, int want_h) {
    screen_crop(src_w, src_h, cx, cy, cw, ch);
    w = std::min(want_w, cw);
    h = std::min(want_h, ch);
    std::fill_n(cols, w, 0u);
    std::fill_n(rows, h, 0u);
    for (int x = 0; x < cw; ++x) ++cols[static_cast<int>(int64_t{x} * w / cw)];
    for (int y = 0; y < ch; ++y) ++rows[static_cast<int>(int64_t{y} * h / ch)];
    std::fill_n(band, kBand * w, 0u);
    done = 0;
  }
  void add(int x, int y, uint8_t v) {
    x -= cx;
    y -= cy;
    if (x < 0 || y < 0 || x >= cw || y >= ch) return;
    const int ty = static_cast<int>(int64_t{y} * h / ch);
    if (ty < done) return;
    while (ty >= done + kBand) flush();
    band[(ty % kBand) * w + static_cast<int>(int64_t{x} * w / cw)] += v;
  }
  void flush() {
    if (done >= h) return;
    uint32_t* sums = band + (done % kBand) * w;
    for (int tx = 0; tx < w; ++tx) {
      const uint32_t n = cols[tx] * rows[done];
      out[done * w + tx] = n ? static_cast<uint8_t>(sums[tx] / n) : 255;
      sums[tx] = 0;
    }
    ++done;
  }
  void finish() {
    while (done < h) flush();
  }
};

// Where make_screen() keeps things in the work buffers. In the sums buffer:
// the file it writes (header, picture, thumbnail), the band of sums, the
// counts per column and row, then a whole PNG file. The large thumbnail's
// buffer takes a BMP's rows, then the thumbnail's sums.
constexpr size_t kScreenFileBytes = kHeader + kScreenW * kScreenH + kThumbW * kThumbH;
constexpr size_t kBandAt = (kScreenFileBytes + 15) / 16 * 16;
constexpr size_t kCountsAt = kBandAt + ScreenShrinker::kBand * kScreenW * sizeof(uint32_t);
constexpr size_t kPngAt = kCountsAt + (kScreenW + kScreenH) * sizeof(uint32_t);
static_assert(kPngAt + kMaxPngFile <= kLargeW * kLargeH * sizeof(uint32_t));
static_assert(kThumbW * kThumbH * sizeof(uint32_t) <= kLargeW * kLargeH);

struct JpegFile {
  File* f;
  uint32_t pos;
  ScreenShrinker* shrink;
};

size_t jpeg_file_in(JDEC* jd, uint8_t* buf, size_t n) {
  auto* in = static_cast<JpegFile*>(jd->device);
  n = std::min<size_t>(n, in->f->size() - std::min(in->pos, in->f->size()));
  if (buf != nullptr) {
    const int32_t got = in->f->read_at(in->pos, buf, static_cast<uint32_t>(n));
    n = got > 0 ? static_cast<size_t>(got) : 0;
  }
  in->pos += static_cast<uint32_t>(n);
  return n;
}

int jpeg_file_out(JDEC* jd, void* bitmap, JRECT* r) {
  auto* in = static_cast<JpegFile*>(jd->device);
  const auto* px = static_cast<const uint8_t*>(bitmap);
  for (int y = r->top; y <= r->bottom; ++y) {
    for (int x = r->left; x <= r->right; ++x) in->shrink->add(x, y, *px++);
  }
  return 1;
}

bool screen_from_jpeg(File& f, Work& wk, ScreenShrinker& s) {
  JDEC jd;
  JpegFile in{&f, 0, &s};
  if (jd_prepare(&jd, jpeg_file_in, wk.pool, kPoolBytes, &in) != JDR_OK) return false;
  int w, h, cx, cy, cw, ch;
  screen_size(jd.width, jd.height, w, h);
  screen_crop(jd.width, jd.height, cx, cy, cw, ch);
  // Let the decoder shrink as far as it can while staying above the picture kept.
  uint8_t scale = 0;
  while (scale < 3 && (cw >> (scale + 1)) >= w && (ch >> (scale + 1)) >= h) ++scale;
  s.start((jd.width + (1 << scale) - 1) >> scale, (jd.height + (1 << scale) - 1) >> scale, w, h);
  return jd_decomp(&jd, jpeg_file_out, scale) == JDR_OK;
}

bool screen_from_png(File& f, Work& wk, ScreenShrinker& s) {
  const uint32_t len = f.size();
  auto* data = reinterpret_cast<uint8_t*>(wk.sum) + kPngAt;
  if (len > kMaxPngFile || f.read_at(0, data, len) != static_cast<int32_t>(len)) return false;
  int pw, ph, comp;
  if (!stbi_info_from_memory(data, static_cast<int>(len), &pw, &ph, &comp)) return false;
  if (uint64_t{static_cast<uint32_t>(pw)} * static_cast<uint32_t>(ph) > kMaxPngPixels) return false;
  uint8_t* grey = stbi_load_from_memory(data, static_cast<int>(len), &pw, &ph, &comp, 1);
  if (grey == nullptr) return false;
  int w, h;
  screen_size(pw, ph, w, h);
  s.start(pw, ph, w, h);
  for (int y = s.cy; y < s.cy + s.ch; ++y) {
    for (int x = s.cx; x < s.cx + s.cw; ++x) s.add(x, y, grey[y * pw + x]);
  }
  stbi_image_free(grey);
  return true;
}

bool screen_from_bmp(File& f, Work& wk, ScreenShrinker& s) {
  bmp::Info info;
  if (!bmp::read_info(f, info) || info.row_bytes + static_cast<uint32_t>(info.w) > kLargeW * kLargeH) return false;
  int w, h;
  screen_size(info.w, info.h, w, h);
  s.start(info.w, info.h, w, h);
  uint8_t* raw = wk.large;
  uint8_t* grey = wk.large + info.row_bytes;
  for (int y = s.cy; y < s.cy + s.ch; ++y) {  // only the rows that are kept
    if (!bmp::read_row(f, info, y, raw, grey)) return false;
    for (int x = s.cx; x < s.cx + s.cw; ++x) s.add(x, y, grey[x]);
  }
  return true;
}

// Reads a screen picture's header; false when it is not one.
bool screen_header(File& f, int& w, int& h, int& tw, int& th) {
  uint8_t head[kHeader];
  if (f.read_at(0, head, kHeader) != static_cast<int32_t>(kHeader) || std::memcmp(head, kScreenMagic, 4) != 0) {
    return false;
  }
  w = get16(head + 4);
  h = get16(head + 6);
  tw = get16(head + 8);
  th = get16(head + 10);
  return w > 0 && h > 0 && w <= kScreenW && h <= kScreenH && tw > 0 && th > 0 && tw <= kThumbW && th <= kThumbH &&
         f.size() >= kHeader + static_cast<uint32_t>(w * h + tw * th);
}

}  // namespace

bool make_screen(Storage& storage, const char* picture_path, const char* out_path) {
  Work& wk = work();
  if (!wk.ok()) return false;
  File* f = storage.open(picture_path);
  if (f == nullptr) return false;
  auto* file = reinterpret_cast<uint8_t*>(wk.sum);
  ScreenShrinker s;
  s.band = reinterpret_cast<uint32_t*>(file + kBandAt);
  s.cols = reinterpret_cast<uint32_t*>(file + kCountsAt);
  s.rows = s.cols + kScreenW;
  s.out = file + kHeader;
  uint8_t magic[4] = {};
  bool ok = f->read_at(0, magic, 4) == 4;
  if (ok && magic[0] == 0xFF && magic[1] == 0xD8) {
    ok = screen_from_jpeg(*f, wk, s);
  } else if (ok && std::memcmp(magic, "\x89PNG", 4) == 0) {
    ok = screen_from_png(*f, wk, s);
  } else if (ok && magic[0] == 'B' && magic[1] == 'M') {
    ok = screen_from_bmp(*f, wk, s);
  } else {
    ok = false;
  }
  storage.close(f);
  if (!ok) return false;
  s.finish();

  // The thumbnail, from the picture; its sums go in the large thumbnail's buffer.
  int tw, th;
  fit(s.w, s.h, kThumbW, kThumbH, tw, th);
  const Shrinker t{s.w, s.h, tw, th, reinterpret_cast<uint32_t*>(wk.large)};
  t.start();
  for (int y = 0; y < s.h; ++y) {
    for (int x = 0; x < s.w; ++x) t.add(x, y, s.out[y * s.w + x]);
  }
  t.finish(s.out + s.w * s.h);

  std::memcpy(file, kScreenMagic, 4);
  put16(file + 4, s.w);
  put16(file + 6, s.h);
  put16(file + 8, tw);
  put16(file + 10, th);
  return storage.write(out_path, file, static_cast<uint32_t>(kHeader + s.w * s.h + tw * th));
}

const CoverImage* load_screen(Storage& storage, const char* path) {
  static CoverImage img;
  Work& wk = work();
  if (!wk.ok()) return nullptr;
  File* f = storage.open(path);
  if (f == nullptr) return nullptr;
  int w = 0, h = 0, tw, th;
  bool ok = screen_header(*f, w, h, tw, th);
  img.px = reinterpret_cast<uint8_t*>(wk.sum);
  if (ok) ok = f->read_at(kHeader, img.px, static_cast<uint32_t>(w * h)) == w * h;
  storage.close(f);
  img.w = ok ? w : 0;
  img.h = ok ? h : 0;
  return ok ? &img : nullptr;
}

bool load_thumb(Storage& storage, const char* path, CoverImage& out) {
  File* f = storage.open(path);
  if (f == nullptr) return false;
  int w = 0, h = 0, tw = 0, th = 0;
  bool ok = screen_header(*f, w, h, tw, th);
  if (ok) ok = f->read_at(static_cast<uint32_t>(kHeader + w * h), out.px, static_cast<uint32_t>(tw * th)) == tw * th;
  storage.close(f);
  out.w = ok ? tw : 0;
  out.h = ok ? th : 0;
  return ok;
}

}  // namespace vellum::covers

#include "vellum/highlights.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/textfile.h"
#include "vellum/upload_page.h"

namespace vellum {
namespace {

constexpr uint32_t kFileBytes = Highlights::kMax * 1024;

char* file_buffer() {
  static char* buf = new (std::nothrow) char[kFileBytes];
  return buf;
}

// Copies s into out with tabs and line breaks as spaces.
void copy_flat(const char* s, char* out, size_t size) {
  size_t n = 0;
  for (; *s != '\0' && n + 1 < size; ++s) out[n++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
  // Never end inside a UTF-8 character: back off to its first byte.
  while (n > 0 && (static_cast<uint8_t>(*s) & 0xC0) == 0x80) {
    --n;
    --s;
  }
  out[n] = '\0';
}

void append(char* buf, size_t& n, size_t cap, const char* s, char end) {
  for (; *s != '\0' && n + 2 < cap; ++s) buf[n++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
  if (n + 1 < cap) buf[n++] = end;
}

// "3:12.0-12.57"
bool parse_where(std::string_view s, Highlight& h) {
  unsigned c, p0, o0, p1, o1;
  char tmp[64];
  textfile::copy(s, tmp, sizeof(tmp));
  if (std::sscanf(tmp, "%u:%u.%u-%u.%u", &c, &p0, &o0, &p1, &o1) != 5) return false;
  h.begin = {static_cast<uint16_t>(c), {p0, o0}};
  h.end = {p1, o1};
  return true;
}

bool before(const Highlight& a, const Highlight& b) {
  if (a.begin.chapter != b.begin.chapter) return a.begin.chapter < b.begin.chapter;
  return a.begin.text < b.begin.text;
}

}  // namespace

Highlights::Highlights() : items_(new(std::nothrow) Highlight[kMax]) {}

Highlights::~Highlights() { delete[] items_; }

void Highlights::load(Storage& storage) {
  count_ = 0;
  next_id_ = 1;
  char* buf = file_buffer();
  if (buf == nullptr || items_ == nullptr) return;
  const int32_t n = textfile::read(storage, kPath, buf, kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(buf, static_cast<size_t>(n)), [&](const std::string_view* f, int count) {
    if (count < 7 || count_ >= kMax) return;
    Highlight& h = items_[count_];
    h = {};
    h.id = textfile::to_uint(f[0]);
    if (h.id == 0 || !parse_where(f[3], h)) return;
    textfile::copy(f[1], h.path, sizeof(h.path));
    textfile::copy(f[2], h.title, sizeof(h.title));
    h.percent = static_cast<uint8_t>(std::min<uint32_t>(textfile::to_uint(f[4]), 100));
    textfile::copy(f[5], h.date, sizeof(h.date));
    textfile::copy(f[6], h.text, sizeof(h.text));
    if (count > 7) textfile::copy(f[7], h.note, sizeof(h.note));
    next_id_ = std::max(next_id_, h.id + 1);
    ++count_;
  });
}

bool Highlights::save(Storage& storage) const {
  char* buf = file_buffer();
  if (buf == nullptr || items_ == nullptr) return false;
  size_t n = 0;
  for (int k = 0; k < count_; ++k) {
    const Highlight& h = items_[k];
    char num[16], where[64];
    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(h.id));
    append(buf, n, kFileBytes, num, '\t');
    append(buf, n, kFileBytes, h.path, '\t');
    append(buf, n, kFileBytes, h.title, '\t');
    std::snprintf(where, sizeof(where), "%u:%lu.%lu-%lu.%lu", unsigned{h.begin.chapter},
                  static_cast<unsigned long>(h.begin.text.paragraph), static_cast<unsigned long>(h.begin.text.offset),
                  static_cast<unsigned long>(h.end.paragraph), static_cast<unsigned long>(h.end.offset));
    append(buf, n, kFileBytes, where, '\t');
    std::snprintf(num, sizeof(num), "%u", unsigned{h.percent});
    append(buf, n, kFileBytes, num, '\t');
    append(buf, n, kFileBytes, h.date, '\t');
    append(buf, n, kFileBytes, h.text, '\t');
    append(buf, n, kFileBytes, h.note, '\n');
  }
  return storage.write(kPath, buf, static_cast<uint32_t>(n));
}

int Highlights::find(uint32_t id) const {
  for (int k = 0; k < count_; ++k) {
    if (items_[k].id == id) return k;
  }
  return -1;
}

int Highlights::of_book(const char* path, int chapter, int* out, int max) const {
  int n = 0;
  for (int k = 0; k < count_ && n < max; ++k) {
    if (std::strcmp(items_[k].path, path) != 0) continue;
    if (chapter >= 0 && items_[k].begin.chapter != chapter) continue;
    out[n++] = k;
  }
  std::sort(out, out + n, [&](int a, int b) { return before(items_[a], items_[b]); });
  return n;
}

int Highlights::add(const Highlight& h) {
  if (items_ == nullptr || count_ >= kMax) return -1;
  Highlight& slot = items_[count_];
  slot = h;
  slot.id = next_id_++;
  copy_flat(h.text, slot.text, sizeof(slot.text));
  copy_flat(h.note, slot.note, sizeof(slot.note));
  return count_++;
}

void Highlights::remove(int k) {
  if (k < 0 || k >= count_) return;
  for (int i = k + 1; i < count_; ++i) items_[i - 1] = items_[i];
  --count_;
}

void Highlights::rename_book(const char* from, const char* to) {
  for (int k = 0; k < count_; ++k) {
    if (std::strcmp(items_[k].path, from) == 0) std::snprintf(items_[k].path, sizeof(Highlight::path), "%s", to);
  }
}

bool Highlights::set_note(uint32_t id, const char* note) {
  const int k = find(id);
  if (k < 0) return false;
  copy_flat(note, items_[k].note, sizeof(items_[k].note));
  return true;
}

void Highlights::write_json(EmitFn emit, void* ctx) const {
  emit(ctx, "[", 1);
  // One at a time, room for every field escaped at its longest; too large
  // for a server task's stack.
  static char item[8192];
  int sent = 0;
  for (int k = 0; k < count_; ++k) {
    const Highlight& h = items_[k];
    size_t len = static_cast<size_t>(std::snprintf(item, sizeof(item), "%s{\"id\":%lu,\"book\":\"", sent > 0 ? "," : "",
                                                   static_cast<unsigned long>(h.id)));
    bool ok = json_escape(h.path, item, sizeof(item), len);
    len += static_cast<size_t>(std::snprintf(item + len, sizeof(item) - len, "\",\"title\":\""));
    ok = ok && json_escape(h.title, item, sizeof(item), len);
    len += static_cast<size_t>(std::snprintf(item + len, sizeof(item) - len,
                                             "\",\"chapter\":%u,\"percent\":%u,\"date\":\"", unsigned{h.begin.chapter},
                                             unsigned{h.percent}));
    ok = ok && json_escape(h.date, item, sizeof(item), len);
    len += static_cast<size_t>(std::snprintf(item + len, sizeof(item) - len, "\",\"text\":\""));
    ok = ok && json_escape(h.text, item, sizeof(item), len);
    len += static_cast<size_t>(std::snprintf(item + len, sizeof(item) - len, "\",\"note\":\""));
    ok = ok && json_escape(h.note, item, sizeof(item), len);
    len += static_cast<size_t>(std::snprintf(item + len, sizeof(item) - len, "\"}"));
    if (!ok || len >= sizeof(item)) continue;
    emit(ctx, item, len);
    ++sent;
  }
  emit(ctx, "]", 1);
}

}  // namespace vellum

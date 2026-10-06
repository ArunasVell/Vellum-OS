#include "vellum/kosync.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "vellum/textfile.h"
#include "vellum/upload_page.h"

namespace vellum::kosync {
namespace {

// ---- MD5 (RFC 1321) -----------------------------------------------------------

class Md5 {
 public:
  void update(const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
      block_[fill_++] = p[i];
      if (fill_ == 64) {
        transform();
        fill_ = 0;
      }
    }
    bytes_ += len;
  }

  void finish(char out[33]) {
    const uint64_t bits = bytes_ * 8;
    const uint8_t one = 0x80;
    update(&one, 1);
    const uint8_t zero = 0;
    while (fill_ != 56) update(&zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = static_cast<uint8_t>(bits >> (8 * i));
    update(length, 8);
    static constexpr char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
      const auto b = static_cast<uint8_t>(state_[i / 4] >> (8 * (i % 4)));
      out[2 * i] = kHex[b >> 4];
      out[2 * i + 1] = kHex[b & 15];
    }
    out[32] = '\0';
  }

 private:
  static uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

  void transform() {
    static constexpr uint32_t kK[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static constexpr int kShift[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      m[i] = uint32_t{block_[4 * i]} | uint32_t{block_[4 * i + 1]} << 8 | uint32_t{block_[4 * i + 2]} << 16 |
             uint32_t{block_[4 * i + 3]} << 24;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    for (int i = 0; i < 64; ++i) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const uint32_t next = d;
      d = c;
      c = b;
      b = b + rotl(a + f + kK[i] + m[g], kShift[(i / 16) * 4 + i % 4]);
      a = next;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
  }

  uint32_t state_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  uint8_t block_[64] = {};
  size_t fill_ = 0;
  uint64_t bytes_ = 0;
};

// ---- Small JSON answers -------------------------------------------------------

// The value of "key" at the top of a flat JSON object: where it starts, or
// nullptr. Good enough for the server's answers, which nest nothing.
const char* json_value(const char* json, const char* key) {
  const size_t n = std::strlen(key);
  for (const char* p = std::strchr(json, '"'); p != nullptr; p = std::strchr(p + 1, '"')) {
    if (std::strncmp(p + 1, key, n) != 0 || p[n + 1] != '"') continue;
    const char* v = p + n + 2;
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') ++v;
    if (*v != ':') continue;
    ++v;
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') ++v;
    return v;
  }
  return nullptr;
}

// A string value, with simple escapes undone (\uXXXX becomes '?').
bool json_string(const char* json, const char* key, char* out, size_t size) {
  const char* v = json_value(json, key);
  if (v == nullptr || *v != '"' || size == 0) return false;
  size_t len = 0;
  for (++v; *v != '\0' && *v != '"'; ++v) {
    char c = *v;
    if (c == '\\' && v[1] != '\0') {
      c = *++v;
      if (c == 'n' || c == 't' || c == 'r') c = ' ';
      if (c == 'u') {
        for (int i = 0; i < 4 && v[1] != '\0'; ++i) ++v;
        c = '?';
      }
    }
    if (len + 1 < size) out[len++] = c;
  }
  out[len] = '\0';
  return *v == '"';
}

// A number value (a quoted one too: some servers send "0.42").
bool json_number(const char* json, const char* key, double& out) {
  const char* v = json_value(json, key);
  if (v == nullptr) return false;
  if (*v == '"') ++v;
  char* end = nullptr;
  out = std::strtod(v, &end);
  return end != v;
}

// An answer gathered into a small buffer (the server's are short).
struct Answer {
  char text[768] = {};
  size_t len = 0;

  static bool add(void* ctx, const uint8_t* data, uint32_t n, uint32_t) {
    auto& a = *static_cast<Answer*>(ctx);
    const size_t take = std::min<size_t>(n, sizeof(a.text) - 1 - a.len);
    std::memcpy(a.text + a.len, data, take);
    a.len += take;
    a.text[a.len] = '\0';
    return true;
  }
};

Result net_result(NetResult r) {
  switch (r) {
    case NetResult::NoNetwork:
      return Result::NoNetwork;
    case NetResult::NoWifi:
      return Result::NoWifi;
    default:
      return Result::NoServer;
  }
}

// Sends a request to the account's server with its login headers.
Result send(Network& net, const Account& a, const char* method, const char* path, const char* body, int& status,
            Answer& answer) {
  char url[256];
  char headers[256];
  const int u = std::snprintf(url, sizeof(url), "%s%s", a.server[0] != '\0' ? a.server : kDefaultServer, path);
  const int h = std::snprintf(headers, sizeof(headers),
                              "Accept: application/vnd.koreader.v1+json\r\n"
                              "Content-Type: application/json\r\n"
                              "x-auth-user: %s\r\nx-auth-key: %s\r\n",
                              a.user, a.key);
  if (u < 0 || static_cast<size_t>(u) >= sizeof(url) || h < 0 || static_cast<size_t>(h) >= sizeof(headers)) {
    return Result::NoServer;
  }
  HttpRequest req;
  req.method = method;
  req.url = url;
  req.headers = headers;
  req.body = body;
  req.body_len = body != nullptr ? static_cast<uint32_t>(std::strlen(body)) : 0;
  const NetResult r = net.request(req, &Answer::add, &answer);
  status = req.status;
  if (r != NetResult::Ok) return net_result(r);
  if (status == 401 || status == 403) return Result::BadLogin;
  return Result::Ok;
}

bool is_hex32(std::string_view s) {
  if (s.size() != 32) return false;
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// Text for a header or a file line: no control characters.
bool plain(const char* s) {
  for (; *s != '\0'; ++s) {
    if (static_cast<unsigned char>(*s) < 0x20 || *s == 0x7F) return false;
  }
  return true;
}

}  // namespace

void md5_hex(const void* data, size_t len, char out[33]) {
  Md5 m;
  m.update(data, len);
  m.finish(out);
}

// ---- The account --------------------------------------------------------------

void load_account(Storage& storage, Account& out) {
  out = {};
  char buf[512];
  const int32_t n = textfile::read(storage, kAccountPath, buf, sizeof(buf));
  if (n <= 0) return;
  std::string_view text(buf, static_cast<size_t>(n));
  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, eq);
    const std::string_view value = line.substr(eq + 1);
    if (key == "server") textfile::copy(value, out.server, sizeof(out.server));
    if (key == "user") textfile::copy(value, out.user, sizeof(out.user));
    if (key == "key" && is_hex32(value)) textfile::copy(value, out.key, sizeof(out.key));
    if (key == "create") out.create = value == "1";
    if (key == "auto") out.automatic = value == "1";
    if (key == "method") out.method = value == "1" ? Method::FileName : Method::Binary;
  }
}

bool save_account(Storage& storage, const Account& a) {
  char buf[512];
  const int n =
      std::snprintf(buf, sizeof(buf), "server=%s\nuser=%s\nkey=%s\ncreate=%d\nauto=%d\nmethod=%d\n", a.server, a.user,
                    a.key, a.create ? 1 : 0, a.automatic ? 1 : 0, a.method == Method::FileName ? 1 : 0);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) return false;
  return storage.write(kAccountPath, buf, static_cast<uint32_t>(n));
}

bool account_from_form(const char* body, size_t len, Account& a) {
  char value[128];
  if (form_field(body, len, "forget", value, sizeof(value)) && value[0] == '1') {
    a = {};
    return true;
  }
  Account next = a;
  char user[sizeof(a.user)];
  char server[sizeof(a.server)];
  char password[128];
  if (!form_field(body, len, "user", user, sizeof(user)) || user[0] == '\0' || !plain(user)) return false;
  if (!form_field(body, len, "server", server, sizeof(server)) || !plain(server)) return false;
  size_t n = std::strlen(server);
  while (n > 0 && (server[n - 1] == '/' || server[n - 1] == ' ')) server[--n] = '\0';
  if (n == 0) std::snprintf(server, sizeof(server), "%s", kDefaultServer);
  if (std::strncmp(server, "https://", 8) != 0 && std::strncmp(server, "http://", 7) != 0) return false;
  if (!form_field(body, len, "password", password, sizeof(password))) password[0] = '\0';
  if (password[0] != '\0') {
    md5_hex(password, std::strlen(password), next.key);
  } else if (std::strcmp(user, a.user) != 0) {
    return false;  // a new user needs a password
  }
  std::snprintf(next.user, sizeof(next.user), "%s", user);
  std::snprintf(next.server, sizeof(next.server), "%s", server);
  if (form_field(body, len, "create", value, sizeof(value))) next.create = value[0] == '1';
  if (form_field(body, len, "auto", value, sizeof(value))) next.automatic = value[0] == '1';
  if (form_field(body, len, "method", value, sizeof(value))) {
    next.method = value[0] == '1' ? Method::FileName : Method::Binary;
  }
  if (!next.ready()) return false;
  a = next;
  return true;
}

bool account_json(const Account& a, char* out, size_t size) {
  size_t len = 0;
  int n = std::snprintf(out, size, "{\"server\":\"");
  if (n < 0 || static_cast<size_t>(n) >= size) return false;
  len = static_cast<size_t>(n);
  if (!json_escape(a.server[0] != '\0' ? a.server : kDefaultServer, out, size, len)) return false;
  n = std::snprintf(out + len, size - len, "\",\"user\":\"");
  if (n < 0 || static_cast<size_t>(n) >= size - len) return false;
  len += static_cast<size_t>(n);
  if (!json_escape(a.ready() ? a.user : "", out, size, len)) return false;
  n = std::snprintf(out + len, size - len, "\",\"auto\":%d,\"method\":%d}", a.automatic ? 1 : 0,
                    a.method == Method::FileName ? 1 : 0);
  return n >= 0 && static_cast<size_t>(n) < size - len;
}

// ---- Books and positions ------------------------------------------------------

bool document_id(Storage& storage, const char* path, Method method, char out[33]) {
  if (method == Method::FileName) {
    const char* name = std::strrchr(path, '/');
    name = name != nullptr ? name + 1 : path;
    md5_hex(name, std::strlen(name), out);
    return true;
  }
  // KOReader's util.partialMD5: 1 KB at 0, then at 1 KB << 2i for i = 0..10
  // (1 KB, 4 KB, 16 KB ... 1 GB), until one starts past the end.
  File* f = storage.open(path);
  if (f == nullptr) return false;
  Md5 m;
  uint8_t sample[1024];
  bool ok = true;
  for (int i = -1; i <= 10; ++i) {
    const uint64_t at = i < 0 ? 0 : uint64_t{1024} << (2 * i);
    if (at >= f->size()) break;
    const int32_t n = f->read_at(static_cast<uint32_t>(at), sample, sizeof(sample));
    if (n <= 0) {
      ok = false;
      break;
    }
    m.update(sample, static_cast<size_t>(n));
  }
  storage.close(f);
  if (ok) m.finish(out);
  return ok;
}

double share_at(const Document& doc, int chapter, int page, int page_count) {
  double before = 0;
  double total = 0;
  for (int i = 0; i < doc.chapter_count(); ++i) {
    const double w = doc.chapter_weight(i);
    if (i < chapter) before += w;
    total += w;
  }
  if (total <= 0) return 0;
  const double within = page_count > 0 ? static_cast<double>(page) / page_count : 0;
  return (before + doc.chapter_weight(chapter) * within) / total;
}

Spot spot_at(const Document& doc, double share) {
  double total = 0;
  for (int i = 0; i < doc.chapter_count(); ++i) total += doc.chapter_weight(i);
  Spot s;
  if (total <= 0 || doc.chapter_count() == 0) return s;
  const double at = std::fmin(std::fmax(share, 0.0), 1.0) * total;
  double before = 0;
  for (int i = 0; i < doc.chapter_count(); ++i) {
    const double w = doc.chapter_weight(i);
    if (at < before + w || i + 1 == doc.chapter_count()) {
      s.chapter = i;
      s.within = w > 0 ? std::fmin((at - before) / w, 1.0) : 0;
      return s;
    }
    before += w;
  }
  return s;
}

int page_at(double within, int page_count) {
  if (page_count <= 1) return 0;
  const int page = static_cast<int>(std::lround(within * page_count));
  return page < 0 ? 0 : page >= page_count ? page_count - 1 : page;
}

int xpointer_chapter(const char* xpointer) {
  const char* p = std::strstr(xpointer, "DocFragment[");
  if (p == nullptr) return -1;
  const int n = std::atoi(p + 12);
  return n >= 1 ? n - 1 : -1;
}

// ---- Requests -----------------------------------------------------------------

bool push_body(const char* document, double share, const char* where, const char* device_id, char* out, size_t size) {
  size_t len = 0;
  int n = std::snprintf(out, size, "{\"document\":\"%s\",\"percentage\":%.4f,\"progress\":\"", document, share);
  if (n < 0 || static_cast<size_t>(n) >= size) return false;
  len = static_cast<size_t>(n);
  if (!json_escape(where, out, size, len)) return false;
  n = std::snprintf(out + len, size - len, "\",\"device\":\"Vellum\",\"device_id\":\"%s\"}", device_id);
  return n >= 0 && static_cast<size_t>(n) < size - len;
}

bool parse_progress(const char* json, Progress& out) {
  out = {};
  json_string(json, "progress", out.where, sizeof(out.where));
  double share = -1;
  const bool has_share = json_number(json, "percentage", share) && share >= 0 && share <= 1;
  // Without a share, the chapter of an EPUB xpointer will do.
  if (!has_share && xpointer_chapter(out.where) < 0) {
    out.where[0] = '\0';
    return false;
  }
  out.share = has_share ? share : -1;
  json_string(json, "device", out.device, sizeof(out.device));
  json_string(json, "device_id", out.device_id, sizeof(out.device_id));
  double t = 0;
  if (json_number(json, "timestamp", t) && t > 0) out.timestamp = static_cast<uint32_t>(t);
  return true;
}

Result register_user(Network& net, const Account& a) {
  char body[160];
  size_t len = 0;
  std::snprintf(body, sizeof(body), "{\"username\":\"");
  len = std::strlen(body);
  if (!json_escape(a.user, body, sizeof(body) - 40, len)) return Result::NoServer;
  std::snprintf(body + len, sizeof(body) - len, "\",\"password\":\"%s\"}", a.key);
  int status = 0;
  Answer answer;
  const Result r = send(net, a, "POST", "/users/create", body, status, answer);
  if (r != Result::Ok) return r;
  if (status == 201 || status == 200) return Result::Ok;
  if (status == 402 || status == 409) return Result::Taken;  // the sync server says 402 for a name in use
  return Result::NoServer;
}

Result push(Network& net, const Account& a, const char* document, double share, const char* where,
            const char* device_id) {
  char body[384];
  if (!push_body(document, share, where, device_id, body, sizeof(body))) return Result::NoServer;
  int status = 0;
  Answer answer;
  const Result r = send(net, a, "PUT", "/syncs/progress", body, status, answer);
  if (r != Result::Ok) return r;
  return status == 200 || status == 201 || status == 202 ? Result::Ok : Result::NoServer;
}

Result pull(Network& net, const Account& a, const char* document, Progress& out) {
  char path[64];
  std::snprintf(path, sizeof(path), "/syncs/progress/%s", document);
  int status = 0;
  Answer answer;
  out = {};
  const Result r = send(net, a, "GET", path, nullptr, status, answer);
  if (r != Result::Ok) return r;
  if (status == 404) return Result::None;
  if (status != 200) return Result::NoServer;
  return parse_progress(answer.text, out) ? Result::Ok : Result::None;
}

}  // namespace vellum::kosync

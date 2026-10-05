#include "vellum/catalogues.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include "vellum/textfile.h"
#include "vellum/upload_page.h"

namespace vellum {
namespace {

constexpr uint32_t kFileBytes = Catalogues::kMax * (sizeof(Catalogue) + 8);

// A field for the file and headers: no tabs, line breaks or other control characters.
bool plain(const char* s) {
  for (; *s != '\0'; ++s) {
    if (static_cast<unsigned char>(*s) < 0x20 || *s == 0x7F) return false;
  }
  return true;
}

bool append_json(const char* key, const char* value, bool comma, char* out, size_t size, size_t& len) {
  const int n = std::snprintf(out + len, size - len, "%s\"%s\":\"", comma ? "," : "", key);
  if (n < 0 || static_cast<size_t>(n) >= size - len) return false;
  len += static_cast<size_t>(n);
  if (!json_escape(value, out, size, len) || len + 2 > size) return false;
  out[len++] = '"';
  out[len] = '\0';
  return true;
}

}  // namespace

void Catalogues::load(Storage& storage) {
  count_ = 0;
  // Read now and then (opening the store, the phone page): a buffer for the
  // moment rather than one kept for good.
  std::unique_ptr<char[]> buf(new (std::nothrow) char[kFileBytes]);
  if (!buf) return;
  const int32_t n = textfile::read(storage, kPath, buf.get(), kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(buf.get(), static_cast<size_t>(n)), [&](const std::string_view* f, int fields) {
    if (fields < 2 || count_ >= kMax || f[1].empty()) return;
    Catalogue& c = items_[count_++];
    c = {};
    textfile::copy(f[0], c.name, sizeof(c.name));
    textfile::copy(f[1], c.url, sizeof(c.url));
    if (fields > 2) textfile::copy(f[2], c.user, sizeof(c.user));
    if (fields > 3) textfile::copy(f[3], c.password, sizeof(c.password));
  });
}

bool Catalogues::save(Storage& storage) const {
  std::unique_ptr<char[]> buf(new (std::nothrow) char[kFileBytes]);
  if (!buf) return false;
  size_t len = 0;
  for (int i = 0; i < count_; ++i) {
    const Catalogue& c = items_[i];
    const int n = std::snprintf(buf.get() + len, kFileBytes - len, "%s\t%s\t%s\t%s\n", c.name, c.url, c.user, c.password);
    if (n < 0 || static_cast<size_t>(n) >= kFileBytes - len) return false;
    len += static_cast<size_t>(n);
  }
  return storage.write(kPath, buf.get(), static_cast<uint32_t>(len));
}

bool Catalogues::add(const Catalogue& c) {
  if (count_ >= kMax) return false;
  items_[count_++] = c;
  return true;
}

void Catalogues::remove(int i) {
  if (i < 0 || i >= count_) return;
  for (int k = i; k + 1 < count_; ++k) items_[k] = items_[k + 1];
  --count_;
}

bool Catalogues::write_json(char* out, size_t size) const {
  if (size < 3) return false;
  size_t len = 0;
  out[len++] = '[';
  for (int i = 0; i < count_; ++i) {
    const Catalogue& c = items_[i];
    if (len + 2 > size) return false;
    if (i > 0) out[len++] = ',';
    out[len++] = '{';
    if (!append_json("name", c.name, false, out, size, len) || !append_json("url", c.url, true, out, size, len) ||
        !append_json("user", c.user, true, out, size, len) || len + 3 > size) {
      return false;
    }
    out[len++] = '}';
  }
  out[len++] = ']';
  out[len] = '\0';
  return true;
}

Catalogues::FormResult Catalogues::apply_form(const char* body, size_t len) {
  char action[16];
  if (!form_field(body, len, "action", action, sizeof(action))) return FormResult::Bad;
  if (std::strcmp(action, "remove") == 0) {
    char index[8];
    if (!form_field(body, len, "index", index, sizeof(index))) return FormResult::Bad;
    const uint32_t i = textfile::to_uint(index, UINT32_MAX);
    if (i >= static_cast<uint32_t>(count_)) return FormResult::Bad;
    remove(static_cast<int>(i));
    return FormResult::Ok;
  }
  if (std::strcmp(action, "add") != 0) return FormResult::Bad;
  Catalogue c;
  if (!form_field(body, len, "url", c.url, sizeof(c.url)) || !plain(c.url) ||
      (std::strncmp(c.url, "https://", 8) != 0 && std::strncmp(c.url, "http://", 7) != 0)) {
    return FormResult::Bad;
  }
  if (!form_field(body, len, "name", c.name, sizeof(c.name)) || !plain(c.name)) return FormResult::Bad;
  if (c.name[0] == '\0') {
    // The site's name will do: "https://books.example.org/opds" is books.example.org.
    const char* host = std::strstr(c.url, "://") + 3;
    const size_t n = std::strcspn(host, "/:?");
    textfile::copy(std::string_view(host, n), c.name, sizeof(c.name));
  }
  if (!form_field(body, len, "user", c.user, sizeof(c.user))) c.user[0] = '\0';
  if (!form_field(body, len, "password", c.password, sizeof(c.password))) c.password[0] = '\0';
  if (!plain(c.user) || !plain(c.password)) return FormResult::Bad;
  return add(c) ? FormResult::Ok : FormResult::Full;
}

bool base64(const void* data, size_t len, char* out, size_t size) {
  static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto* p = static_cast<const uint8_t*>(data);
  if ((len + 2) / 3 * 4 + 1 > size) return false;
  size_t n = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t v = uint32_t{p[i]} << 16 | (i + 1 < len ? uint32_t{p[i + 1]} << 8 : 0) |
                       (i + 2 < len ? uint32_t{p[i + 2]} : 0);
    out[n++] = kDigits[(v >> 18) & 63];
    out[n++] = kDigits[(v >> 12) & 63];
    out[n++] = i + 1 < len ? kDigits[(v >> 6) & 63] : '=';
    out[n++] = i + 2 < len ? kDigits[v & 63] : '=';
  }
  out[n] = '\0';
  return true;
}

bool basic_auth_header(const Catalogue& c, char* out, size_t size) {
  if (size == 0) return false;
  out[0] = '\0';
  if (c.user[0] == '\0') return true;
  char pair[sizeof(c.user) + sizeof(c.password)];
  const int n = std::snprintf(pair, sizeof(pair), "%s:%s", c.user, c.password);
  char encoded[(sizeof(pair) + 2) / 3 * 4 + 1];
  if (n < 0 || !base64(pair, static_cast<size_t>(n), encoded, sizeof(encoded))) return false;
  const int m = std::snprintf(out, size, "Authorization: Basic %s\r\n", encoded);
  return m >= 0 && static_cast<size_t>(m) < size;
}

}  // namespace vellum

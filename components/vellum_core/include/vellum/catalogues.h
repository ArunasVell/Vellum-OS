// The reader's own book catalogues (OPDS feeds such as a Calibre server,
// a COPS site or a library's), added on the phone page and listed in the
// store after the built-in ones. A catalogue may ask for a user name and
// password (HTTP basic authentication).
//
// Kept in vellum/catalogues.txt, one per line: name, address, user and
// password separated by tabs. The password is stored as typed, like the
// home Wi-Fi's, since the server needs it as such.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

struct Catalogue {
  char name[48] = {};
  char url[256] = {};
  char user[64] = {};
  char password[64] = {};
};

class Catalogues {
 public:
  static constexpr int kMax = 8;
  static constexpr const char* kPath = "vellum/catalogues.txt";

  void load(Storage& storage);
  bool save(Storage& storage) const;
  int count() const { return count_; }
  const Catalogue& at(int i) const { return items_[i]; }
  // False when kMax are there already.
  bool add(const Catalogue& c);
  void remove(int i);

  // The list for the phone page, without passwords:
  // [{"name":"...","url":"...","user":"..."}, ...]. False when it does not fit.
  bool write_json(char* out, size_t size) const;

  // The phone page's form (POST /catalogues): action=add with name, url,
  // user and password, or action=remove with index.
  enum class FormResult : uint8_t { Ok, Bad, Full };
  FormResult apply_form(const char* body, size_t len);

 private:
  Catalogue items_[kMax];
  int count_ = 0;
};

// The header line that signs in to `c` ("Authorization: Basic ...\r\n"),
// or "" when it needs no user. False when it does not fit.
bool basic_auth_header(const Catalogue& c, char* out, size_t size);

// Base64 of data, NUL-terminated. False when it does not fit.
bool base64(const void* data, size_t len, char* out, size_t size);

}  // namespace vellum

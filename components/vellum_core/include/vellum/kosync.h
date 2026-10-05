// Reading position shared with KOReader through a KOReader sync server
// (github.com/koreader/koreader-sync-server; sync.koreader.rocks by
// default), so a book read on a phone or another reader carries on here.
//
// The server knows a book by an MD5 of some of its bytes ("binary", KOReader's
// default) or of its file name, and a position by a share of the whole book
// (0..1) and a KOReader "xpointer". Vellum cannot point inside a chapter the
// way KOReader does: it sends the share and an xpointer to the chapter's
// start, and takes the share back, landing on the nearest page.
//
// The account is set on the phone page and kept in vellum/kosync.txt; the
// password is never stored, only its MD5, which is what the server checks.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/document.h"
#include "vellum/hal.h"

namespace vellum::kosync {

constexpr const char* kDefaultServer = "https://sync.koreader.rocks";
constexpr const char* kAccountPath = "vellum/kosync.txt";

// How a book is known to the server; both readers must use the same one.
enum class Method : uint8_t {
  Binary,    // MD5 of 1 KB samples at growing offsets (KOReader's default)
  FileName,  // MD5 of the file's name
};

struct Account {
  char server[128] = {};  // e.g. "https://sync.koreader.rocks", without a trailing '/'
  char user[64] = {};
  char key[33] = {};       // MD5 of the password, lowercase hex
  bool create = false;     // register the user on the next sync
  bool automatic = false;  // send the position on closing a book and on sleeping
  Method method = Method::Binary;

  bool ready() const { return user[0] != '\0' && key[0] != '\0'; }
};

// The account on the card; `out` is left signed out when there is none.
void load_account(Storage& storage, Account& out);
bool save_account(Storage& storage, const Account& a);

// The phone page's form (POST /sync): server, user, password (empty keeps
// the one saved for the same user), create, auto and method as 0 or 1;
// forget=1 signs out. Returns false for a value that cannot be used.
bool account_from_form(const char* body, size_t len, Account& a);
// The account for the phone page, without the key:
// {"server":"...","user":"...","auto":0,"method":0}. False when it does not fit.
bool account_json(const Account& a, char* out, size_t size);

// MD5 of data as 32 lowercase hex digits and a NUL.
void md5_hex(const void* data, size_t len, char out[33]);

// The id the server knows the book at `path` by. False when it cannot be read.
bool document_id(Storage& storage, const char* path, Method method, char out[33]);

// Where page `page` of `page_count` in `chapter` starts, as a share of the
// whole book (0..1), weighing chapters by length as Reader::percent() does.
double share_at(const Document& doc, int chapter, int page, int page_count);
// The chapter holding `share`, and how far into it (0..1).
struct Spot {
  int chapter = 0;
  double within = 0;
};
Spot spot_at(const Document& doc, double share);
// The page of `page_count` that starts nearest `within`.
int page_at(double within, int page_count);

// What the server holds for a book.
struct Progress {
  double share = -1;      // "percentage"; below 0 when the server has none
  char where[128] = {};   // "progress": a KOReader xpointer (or a page number)
  char device[48] = {};   // the device that sent it
  char device_id[40] = {};
  uint32_t timestamp = 0;
};

// The chapter an EPUB xpointer such as "/body/DocFragment[12]/body/p[3]"
// points into (11 there), or -1.
int xpointer_chapter(const char* xpointer);

enum class Result : uint8_t {
  Ok,
  NoNetwork,  // no home Wi-Fi is set
  NoWifi,     // it could not be joined
  NoServer,   // the server could not be reached or answered oddly
  BadLogin,   // the server does not know this user and password
  Taken,      // registering: the name belongs to someone else
  None,       // pulling: nothing saved for this book yet
};

// The server's requests. `device_id` tells this reader apart from others.
Result register_user(Network& net, const Account& a);
Result push(Network& net, const Account& a, const char* document, double share, const char* where,
            const char* device_id);
Result pull(Network& net, const Account& a, const char* document, Progress& out);

// Builds the body of a push; false when it does not fit.
bool push_body(const char* document, double share, const char* where, const char* device_id, char* out,
               size_t size);
// Reads the answer of a pull. False when it holds no position.
bool parse_progress(const char* json, Progress& out);

}  // namespace vellum::kosync

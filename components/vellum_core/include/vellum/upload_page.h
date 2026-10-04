// The page a phone or computer opens to send books to the reader.
//
// It talks to the device over three requests, which every board's transfer
// server implements the same way:
//   GET  /                  this page
//   GET  /books             JSON list: [{"name": "Book.epub", "size": 123}, ...]
//   POST /upload?name=NAME  the file as the request body (a book, or a .ttf or
//                           .otf font for the fonts folder); 200 when saved,
//                           507 when the card is full, 4xx/5xx otherwise
//   POST /time              the phone's local time, "YYYY-MM-DD HH:MM:SS",
//                           to set the reader's clock
//
// A second page, opened from Settings › Updates, sets the home Wi-Fi and
// installs an update file:
//   GET  /setup             that page
//   GET  /info              JSON: {"version": "0.1.42", "home_network": "..."}
//   POST /wifi              form body ssid=...&password=...; 200 when saved
//   POST /update            a Vellum .bin as the body; 200 once installed
#pragma once

#include <cstddef>

#include "vellum/hal.h"

namespace vellum {

extern const char kUploadPage[];
extern const unsigned kUploadPageSize;
extern const char kSetupPage[];
extern const unsigned kSetupPageSize;

// Helpers for the transfer servers, shared so every board treats names the same.

// Decodes %XX escapes and '+' (a space in a query string) into out.
// Returns false when the result does not fit or an escape is malformed.
bool url_decode(const char* in, size_t in_len, char* out, size_t size);

// Finds `key` in a form body ("a=1&b=two") and URL-decodes its value into
// out. Returns false when it is missing, malformed or does not fit.
bool form_field(const char* body, size_t len, const char* key, char* out, size_t size);

// Turns a file name sent by a browser into one that is safe to save in the
// books folder: no folders, no characters FAT rejects, no leading dots, at
// most 120 bytes, and ending in ".epub". Returns false for names that are
// not EPUB books or are empty once cleaned.
bool safe_book_name(const char* in, char* out, size_t size);
// The same for fonts (.ttf or .otf), which go in the fonts folder.
bool safe_font_name(const char* in, char* out, size_t size);

// Parses "YYYY-MM-DD HH:MM:SS" (the body of POST /time). Returns false for
// anything else, including dates outside 2000-2099.
bool parse_datetime(const char* in, size_t len, DateTime& out);

// Appends `s` to out as a JSON string body (without quotes). Returns false
// when it does not fit.
bool json_escape(const char* s, char* out, size_t size, size_t& len);

}  // namespace vellum

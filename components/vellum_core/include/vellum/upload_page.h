// The page a phone or computer opens to send books to the reader.
//
// It talks to the device over three requests, which every board's transfer
// server implements the same way:
//   GET  /                  this page
//   GET  /books             JSON list: [{"name": "Book.epub", "size": 123}, ...]
//   POST /upload?name=NAME  the file as the request body (a book, a .ttf or
//                           .otf font for the fonts folder, a StarDict
//                           dictionary file for the dictionaries folder, or a
//                           JPEG, PNG or BMP picture for the sleep folder); 200
//                           when saved, 507 when the card is full, 4xx/5xx
//                           otherwise
//   POST /time              form body for parse_phone_time(): the phone's
//                           clock and time zone, to set the reader's
//   GET  /highlights        JSON list of the highlights (Highlights::write_json)
//   POST /highlights/note   form body id=...&note=...; 200 when saved
//   POST /highlights/remove form body id=...; 200 when removed
//   GET  /sleep             JSON list of the sleep screen pictures, like /books
//   POST /sleep/remove      form body name=...; 200 when the picture is deleted
//
// Its Settings section changes the reader's settings, the home Wi-Fi, the
// KOReader sync account and the reader's own catalogues (phone_* below
// do the work, so every board answers alike):
//   GET  /settings          JSON: phone_settings_json()
//   POST /settings          form body of settings keys; the reader applies them at once
//   POST /sync              form body for kosync::account_from_form()
//   POST /catalogues        form body for Catalogues::apply_form()
//   POST /wifi              as below
// Each 200 when saved, 400 for a bad value, 507 when the catalogues are full.
// The board then counts TransferStatus::settings_saved up (not for /catalogues:
// the store reads them when it opens).
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
// most 120 bytes, and ending in a book's extension (.epub, .txt, .fb2,
// .fb2.zip, .html, .htm, .mobi, .azw3, .azw, .prc, .pdb, .pdf, .docx, .md,
// .markdown, .rtf, .cbz). Returns false for other names or ones that are
// empty once cleaned.
bool safe_book_name(const char* in, char* out, size_t size);
// The same for fonts (.ttf or .otf), which go in the fonts folder.
bool safe_font_name(const char* in, char* out, size_t size);
// The same for StarDict dictionary files (.ifo, .idx, .dict, .dict.dz, .syn),
// which go in the dictionaries folder.
bool safe_dictionary_name(const char* in, char* out, size_t size);
// The same for sleep screen pictures (.jpg, .jpeg, .png, .bmp), which go in
// the sleep folder.
bool safe_picture_name(const char* in, char* out, size_t size);

// Parses "YYYY-MM-DD HH:MM:SS" (the body of POST /time). Returns false for
// anything else, including dates outside 2000-2099.
bool parse_datetime(const char* in, size_t len, DateTime& out);

// POST /time: utc=YYYY-MM-DD HH:MM:SS (the phone's clock in world time),
// rule=... (its time zone as a POSIX rule, which the page works out from
// the phone's clock changes this year) and zone=Europe/Vilnius (the zone's
// name). Returns false without a good time. A missing or bad rule leaves
// name and rule empty; a missing name gives one like "UTC+2".
struct PhoneTime {
  DateTime utc;
  char name[32];
  char rule[64];
};
bool parse_phone_time(const char* body, size_t len, PhoneTime& out);

// Appends `s` to out as a JSON string body (without quotes). Returns false
// when it does not fit.
bool json_escape(const char* s, char* out, size_t size, size_t& len);

// The phone page's Settings section, on the card's files (phone_settings.cpp).
// GET /settings: {"settings": settings_json(), "home_network": "...",
// "sync": kosync::account_json(), "catalogues": Catalogues::write_json()}.
// `fonts` is TransferInfo::fonts. Returns false when it does not fit.
bool phone_settings_json(Storage& card, const char* fonts, const char* home_network, char* out, size_t size);
// POST /settings, /sync and /catalogues: the form applied and saved.
enum class PhoneSaved : uint8_t { Ok, Bad, Full, NotSaved };
PhoneSaved phone_post_settings(Storage& card, const char* body, size_t len);
PhoneSaved phone_post_sync(Storage& card, const char* body, size_t len);
PhoneSaved phone_post_catalogues(Storage& card, const char* body, size_t len);

}  // namespace vellum

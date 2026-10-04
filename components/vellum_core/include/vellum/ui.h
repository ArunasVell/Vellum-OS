// Shared drawing pieces for Vellum's screens: status bar, labels, book
// covers and list rows, so every screen uses the same margins and marks.
#pragma once

#include <string_view>

#include "vellum/book.h"
#include "vellum/covers.h"
#include "vellum/font_library.h"
#include "vellum/fonts.h"
#include "vellum/framebuffer.h"
#include "vellum/library.h"
#include "vellum/strings.h"

namespace vellum::ui {

constexpr int kSide = 28;        // left and right margin on every screen
constexpr int kStatusBase = 40;  // baseline of the status bar text
constexpr int kStatusRule = 56;  // hairline under the status bar
constexpr int kHintBase = 28;    // control hint baseline, from the bottom edge
constexpr int kRowHeight = 76;   // one book in a list

// The menu typeface (Settings › Menu font), Literata until set. Screens
// draw with these rather than with a built-in font.
void set_menu_fonts(const MenuFonts& f);
const MenuFonts& menu_fonts();
inline const Font& label_font() { return *menu_fonts().label; }                // 16 px
inline const Font& text_font() { return *menu_fonts().text; }                  // 20 px
inline const Font& large_font() { return *menu_fonts().large; }                // 26 px
inline const Font& small_italic_font() { return *menu_fonts().small_italic; }  // 20 px
inline const Font& italic_font() { return *menu_fonts().italic; }              // 26 px
inline const Font& screen_title_font() { return *menu_fonts().title; }         // 40 px italic

void draw_centered(Framebuffer& fb, const Font& font, int baseline, std::string_view text, Color c = Color::Black);
void draw_right(Framebuffer& fb, const Font& font, int right, int baseline, std::string_view text,
                Color c = Color::Black);
// Draws `text`, cut short with an ellipsis when it is wider than max_w px.
void draw_fitted(Framebuffer& fb, const Font& font, int x, int baseline, int max_w, std::string_view text,
                 Color c = Color::Black);
// Word-wraps `text` into at most max_lines lines of max_w px starting at x,
// each centred within that width when `centered`. The last line ends with an
// ellipsis when text is left over. Returns the baseline after the last line.
int draw_wrapped(Framebuffer& fb, const Font& font, int x, int baseline, int max_w, int max_lines,
                 std::string_view text, bool centered, Color c = Color::Black);
// Small capitals-style label: uppercase text with extra letter spacing.
void draw_label(Framebuffer& fb, int x, int baseline, std::string_view text);

// Battery icon ending at `right` with the charge in percent to its left.
// Draws nothing when the level is unknown (negative).
void draw_battery(Framebuffer& fb, int right, int baseline, int percent);
// Time on the left (may be empty), battery on the right, hairline below.
void draw_status_bar(Framebuffer& fb, std::string_view time, int battery_percent);
// Control hint centred at the bottom of the screen.
void draw_hint(Framebuffer& fb, std::string_view text);
// Dotted track with a solid bar for the part done, centred on line y.
void draw_progress(Framebuffer& fb, int x, int y, int w, int done, int total);

// A QR code for `text` (a URL or a Wi-Fi join code) inside a size x size
// square at (x, y), with the white margin scanners need. Returns false when
// the text is too long to encode.
bool draw_qr(Framebuffer& fb, int x, int y, int size, std::string_view text);

// A book's cover: its own picture when there is one (scaled to fill r,
// cropped to fit and dithered), else a typographic cover with frame, title
// and author. Small typographic covers show the title's initial only. Each
// book keeps the same look on every screen.
void draw_cover(Framebuffer& fb, Rect r, std::string_view title, std::string_view author,
                const CoverImage* picture = nullptr);
// A grey picture scaled to fill r (cropping what does not fit), dithered to
// black and white.
void draw_picture(Framebuffer& fb, Rect r, const CoverImage& picture);
// One book in a list: small cover, title, author and how far it is read.
// A selected row is drawn inverted.
void draw_book_row(Framebuffer& fb, Rect r, const LibraryEntry& book, const Strings& text, bool selected,
                   const CoverImage* picture = nullptr);

}  // namespace vellum::ui

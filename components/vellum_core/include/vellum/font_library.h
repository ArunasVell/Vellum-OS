// The typefaces to choose from: Literata (pre-rendered, built in), other
// built-in families stored as font files, and TrueType/OpenType fonts put in
// the card's "fonts" folder (from the phone page). Files are named like
// Google Fonts downloads: "Merriweather-Regular.ttf", "Merriweather-Italic.ttf";
// other weights are ignored.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/font_raster.h"
#include "vellum/fonts.h"
#include "vellum/hal.h"

namespace vellum {

namespace fonts {
// Built-in families rendered on the device (tools/make_ttf_fonts.py).
extern const uint8_t kAtkinsonRegular[];
extern const unsigned kAtkinsonRegularSize;
extern const uint8_t kAtkinsonItalic[];
extern const unsigned kAtkinsonItalicSize;
extern const uint8_t kEbgaramondRegular[];
extern const unsigned kEbgaramondRegularSize;
extern const uint8_t kEbgaramondItalic[];
extern const unsigned kEbgaramondItalicSize;
}  // namespace fonts

// Fonts for menus and screens, one typeface at the sizes the UI uses.
struct MenuFonts {
  const Font* label;         // 16 px: labels, hints, list details
  const Font* text;          // 20 px: list rows and settings
  const Font* large;         // 26 px: large values
  const Font* small_italic;  // 20 px
  const Font* italic;        // 26 px
  const Font* title;         // 40 px italic: screen titles
  const Font* small;         // 16 px: a hint or line too wide for the label size
};

// Fonts for reading: the body text and the chapter headings' italics.
struct BookFonts {
  const Font* body;
  const Font* heading_small;  // 20 px italic
  const Font* heading;        // 26 px italic
  // Rendered fonts are rendered again in place (another family or size
  // behind the same pointers): this changes each time. 0 for Literata.
  uint32_t generation = 0;
};

// Literata's built-in sets.
MenuFonts literata_menu(bool large = false);
BookFonts literata_book(int px);  // px is one of the reading sizes, 23, 26, 30 or 34

// Turns a font file's name into its family and style: "PlayfairDisplay-Italic.ttf"
// is "Playfair Display", italic. Returns false for files that are not a
// regular or italic .ttf/.otf.
bool parse_font_file_name(const char* file, char* family, size_t size, bool& italic);

class FontLibrary {
 public:
  static constexpr int kMaxFamilies = 24;
  static constexpr uint32_t kMaxFileBytes = 6u << 20;  // larger fonts (most CJK) do not fit in memory

  FontLibrary();
  ~FontLibrary();
  FontLibrary(const FontLibrary&) = delete;
  FontLibrary& operator=(const FontLibrary&) = delete;

  // Lists the families: built in first, then the card's (when not nullptr).
  void scan(Storage* card);
  int count() const { return count_; }
  const char* name(int family) const;
  // The family called `name`, or -1.
  int find(const char* name) const;
  bool on_card(int family) const;

  // Renders (or looks up) the fonts in `family`; false when its file cannot
  // be used, and `out` is left as it was. Pointers stay valid until the next
  // call of the same kind with another family or size.
  bool menu(int family, bool large, MenuFonts& out);
  bool book(int family, int px, BookFonts& out);

 private:
  struct Source {
    const uint8_t* data = nullptr;  // built in
    uint32_t size = 0;
    char path[96] = {};  // on the card, when data is nullptr
  };
  struct Family {
    char name[48] = {};
    bool literata = false;
    Source regular;
    Source italic;  // size 0 and no path: none, the regular stands in
  };
  // A face loaded from a source, with the bytes it reads from.
  struct Loaded {
    TtfFace face;
    uint8_t* owned = nullptr;  // a card file read into memory
    int family = -1;
    bool italic = false;
    ~Loaded() { delete[] owned; }
    bool load(const Family& f, bool italic, Storage* card, int family);
  };

  Family families_[kMaxFamilies];
  int count_ = 0;
  Storage* card_ = nullptr;

  // Faces recently read, so menu and book in one family share their kerning.
  Loaded* face(int family, bool italic);
  Loaded faces_[4];
  uint32_t face_used_[4] = {};
  uint32_t clock_ = 0;
  RasterFont menu_fonts_[7];
  RasterFont book_fonts_[3];
  int menu_family_ = -1;
  bool menu_large_ = false;
  static constexpr int kMenuSizes[7] = {16, 20, 26, 20, 26, 40, 16};
  static constexpr int kLargeMenuSizes[7] = {19, 24, 30, 20, 26, 40, 16};
  int book_family_ = -1;
  int book_px_ = 0;
  uint32_t book_generation_ = 0;
};

}  // namespace vellum

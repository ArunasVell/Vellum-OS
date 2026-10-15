#include "vellum/font_library.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

namespace vellum {

MenuFonts literata_menu(bool large) {
  if (large) {
    return {&fonts::literata_19,        &fonts::literata_24,        &fonts::literata_30, &fonts::literata_italic_20,
            &fonts::literata_italic_26, &fonts::literata_italic_40, &fonts::literata_16};
  }
  return {&fonts::literata_16,        &fonts::literata_20,        &fonts::literata_26, &fonts::literata_italic_20,
          &fonts::literata_italic_26, &fonts::literata_italic_40, &fonts::literata_16};
}

BookFonts literata_book(int px) {
  const Font* body = px <= 23   ? &fonts::literata_23
                     : px <= 26 ? &fonts::literata_26
                     : px <= 30 ? &fonts::literata_30
                                : &fonts::literata_34;
  return {body, &fonts::literata_italic_20, &fonts::literata_italic_26};
}

namespace {

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

}  // namespace

bool parse_font_file_name(const char* file, char* family, size_t size, bool& italic) {
  std::string_view name(file);
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos) return false;
  const std::string_view ext = name.substr(dot + 1);
  if (!iequals(ext, "ttf") && !iequals(ext, "otf")) return false;
  name = name.substr(0, dot);
  if (const size_t bracket = name.find('['); bracket != std::string_view::npos) name = name.substr(0, bracket);
  // "Family-Style", "Family-Style-VariableFont_wght" or "Family-VariableFont_wght".
  std::string_view base = name;
  std::string_view style;
  if (const size_t dash = name.find('-'); dash != std::string_view::npos) {
    base = name.substr(0, dash);
    style = name.substr(dash + 1);
  }
  if (const size_t var = style.find("VariableFont"); var != std::string_view::npos) {
    style = style.substr(0, var);
    while (!style.empty() && (style.back() == '-' || style.back() == '_')) style.remove_suffix(1);
  }
  if (const size_t u = base.find('_'); u != std::string_view::npos) {
    // Google's optical-size cuts: "Literata_18pt-Regular".
    const std::string_view rest = base.substr(u + 1);
    if (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest[0]))) base = base.substr(0, u);
  }
  italic = iequals(style, "Italic") || iequals(style, "Oblique") || iequals(style, "RegularItalic");
  const bool regular = style.empty() || iequals(style, "Regular") || iequals(style, "Book") ||
                       iequals(style, "Roman") || iequals(style, "Text");
  if ((!italic && !regular) || base.empty()) return false;

  // Spaces between the words of a CamelCase name: "PlayfairDisplay" ->
  // "Playfair Display", "EBGaramond" -> "EB Garamond".
  size_t len = 0;
  for (size_t i = 0; i < base.size() && len + 2 < size; ++i) {
    const char c = base[i];
    const bool upper = std::isupper(static_cast<unsigned char>(c));
    if (i > 0 && upper) {
      const char prev = base[i - 1];
      const bool next_lower = i + 1 < base.size() && std::islower(static_cast<unsigned char>(base[i + 1]));
      if (std::islower(static_cast<unsigned char>(prev)) ||
          (std::isupper(static_cast<unsigned char>(prev)) && next_lower)) {
        family[len++] = ' ';
      }
    }
    family[len++] = c == '_' ? ' ' : c;
  }
  family[len] = '\0';
  return len > 0;
}

FontLibrary::FontLibrary() { scan(nullptr); }
FontLibrary::~FontLibrary() = default;

const char* FontLibrary::name(int family) const { return family >= 0 && family < count_ ? families_[family].name : ""; }

int FontLibrary::find(const char* name) const {
  for (int i = 0; i < count_; ++i) {
    if (std::strcmp(families_[i].name, name) == 0) return i;
  }
  return -1;
}

bool FontLibrary::on_card(int family) const {
  return family >= 0 && family < count_ && families_[family].regular.data == nullptr && !families_[family].literata;
}

void FontLibrary::scan(Storage* card) {
  card_ = card;
  count_ = 0;
  families_[count_] = {};
  std::snprintf(families_[count_].name, sizeof(families_[0].name), "Literata");
  families_[count_++].literata = true;
  const auto builtin = [&](const char* name, const uint8_t* r, unsigned rs, const uint8_t* i, unsigned is) {
    Family& f = families_[count_++];
    f = {};
    std::snprintf(f.name, sizeof(f.name), "%s", name);
    f.regular.data = r;
    f.regular.size = rs;
    f.italic.data = i;
    f.italic.size = is;
  };
  builtin("Atkinson Hyperlegible Next", fonts::kAtkinsonRegular, fonts::kAtkinsonRegularSize, fonts::kAtkinsonItalic,
          fonts::kAtkinsonItalicSize);
  builtin("EB Garamond", fonts::kEbgaramondRegular, fonts::kEbgaramondRegularSize, fonts::kEbgaramondItalic,
          fonts::kEbgaramondItalicSize);
  const int builtins = count_;

  if (card != nullptr) {
    struct Ctx {
      FontLibrary* lib;
      int builtins;
    } ctx{this, builtins};
    card->list(
        "fonts",
        [](void* p, const char* file, bool is_dir, uint32_t size) {
          auto& c = *static_cast<Ctx*>(p);
          FontLibrary& lib = *c.lib;
          char family[48];
          bool italic = false;
          if (is_dir || size == 0 || size > kMaxFileBytes) return;
          if (!parse_font_file_name(file, family, sizeof(family), italic)) return;
          int index = lib.find(family);
          if (index >= 0 && index < c.builtins) return;  // the built-in one wins
          if (index < 0) {
            if (lib.count_ >= kMaxFamilies) return;
            index = lib.count_++;
            lib.families_[index] = {};
            std::snprintf(lib.families_[index].name, sizeof(lib.families_[index].name), "%s", family);
          }
          Source& s = italic ? lib.families_[index].italic : lib.families_[index].regular;
          std::snprintf(s.path, sizeof(s.path), "fonts/%s", file);
          s.size = size;
        },
        &ctx);
    // A family with only an italic file is not usable for text.
    int kept = builtins;
    for (int i = builtins; i < count_; ++i) {
      if (families_[i].regular.path[0] == '\0') continue;
      if (kept != i) families_[kept] = families_[i];
      ++kept;
    }
    count_ = kept;
  }
  // The rendered fonts may belong to families that moved: render again when asked.
  menu_family_ = -1;
  book_family_ = -1;
  for (Loaded& l : faces_) l.family = -1;
}

bool FontLibrary::Loaded::load(const Family& f, bool want_italic, Storage* card, int index) {
  if (family == index && italic == want_italic && face.loaded()) return true;
  delete[] owned;
  owned = nullptr;
  family = -1;
  const Source& s = want_italic && (f.italic.data != nullptr || f.italic.path[0] != '\0') ? f.italic : f.regular;
  const uint8_t* data = s.data;
  if (data == nullptr) {
    if (card == nullptr || s.path[0] == '\0') return false;
    File* file = card->open(s.path);
    if (file == nullptr) return false;
    const uint32_t size = file->size();
    if (size > kMaxFileBytes) {
      card->close(file);
      return false;
    }
    owned = new (std::nothrow) uint8_t[size];
    const bool read = owned != nullptr && file->read_at(0, owned, size) == static_cast<int32_t>(size);
    card->close(file);
    if (!read || !face.load(owned, size)) {
      delete[] owned;
      owned = nullptr;
      return false;
    }
  } else if (!face.load(data, s.size)) {
    return false;
  }
  family = index;
  italic = want_italic;
  return true;
}

FontLibrary::Loaded* FontLibrary::face(int family, bool italic) {
  int slot = 0;
  for (int i = 0; i < 4; ++i) {
    if (faces_[i].family == family && faces_[i].italic == italic && faces_[i].face.loaded()) {
      slot = i;
      face_used_[slot] = ++clock_;
      return &faces_[slot];
    }
    if (face_used_[i] < face_used_[slot]) slot = i;
  }
  face_used_[slot] = ++clock_;
  return faces_[slot].load(families_[family], italic, card_, family) ? &faces_[slot] : nullptr;
}

bool FontLibrary::menu(int family, bool large, MenuFonts& out) {
  if (family < 0 || family >= count_) return false;
  const Family& f = families_[family];
  if (f.literata) {
    out = literata_menu(large);
    return true;
  }
  if (family != menu_family_ || large != menu_large_) {
    const int* sizes = large ? kLargeMenuSizes : kMenuSizes;
    for (int i = 0; i < 7; ++i) {
      Loaded* l = face(family, i >= 3 && i < 6);
      if (l == nullptr || !menu_fonts_[i].build(l->face, sizes[i], f.name)) {
        menu_family_ = -1;
        return false;
      }
    }
    menu_family_ = family;
    menu_large_ = large;
  }
  out = {&menu_fonts_[0].font(), &menu_fonts_[1].font(), &menu_fonts_[2].font(), &menu_fonts_[3].font(),
         &menu_fonts_[4].font(), &menu_fonts_[5].font(), &menu_fonts_[6].font()};
  return true;
}

bool FontLibrary::book(int family, int px, BookFonts& out) {
  if (family < 0 || family >= count_) return false;
  const Family& f = families_[family];
  if (f.literata) {
    out = literata_book(px);
    return true;
  }
  if (family != book_family_ || px != book_px_) {
    const bool same_family = family == book_family_;
    Loaded* regular = face(family, false);
    if (regular == nullptr || !book_fonts_[0].build(regular->face, px, f.name)) {
      book_family_ = -1;
      return false;
    }
    if (!same_family) {
      Loaded* italic = face(family, true);
      if (italic == nullptr || !book_fonts_[1].build(italic->face, 20, f.name) ||
          !book_fonts_[2].build(italic->face, 26, f.name)) {
        book_family_ = -1;
        return false;
      }
    }
    book_family_ = family;
    book_px_ = px;
    ++book_generation_;
  }
  out = {&book_fonts_[0].font(), &book_fonts_[1].font(), &book_fonts_[2].font(), book_generation_};
  return true;
}

}  // namespace vellum

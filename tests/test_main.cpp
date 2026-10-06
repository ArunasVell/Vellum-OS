// Host unit tests for the portable core. Plain asserts, no framework.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "vellum/app.h"
#include "vellum/battery_log.h"
#include "vellum/bmp.h"
#include "vellum/book.h"
#include "vellum/charset.h"
#include "vellum/controls.h"
#include "vellum/covers.h"
#include "vellum/dictionary.h"
#include "vellum/display_queue.h"
#include "vellum/epub.h"
#include "vellum/font_library.h"
#include "vellum/font_raster.h"
#include "vellum/framebuffer.h"
#include "vellum/highlights.h"
#include "vellum/keyboard.h"
#include "vellum/library.h"
#include "vellum/opds.h"
#include "vellum/peer_transfer.h"
#include "vellum/reader.h"
#include "vellum/reading_stats.h"
#include "vellum/screenshot.h"
#include "vellum/settings.h"
#include "vellum/shelves.h"
#include "vellum/sleep_pictures.h"
#include "vellum/stdio_storage.h"
#include "vellum/store.h"
#include "vellum/strings.h"
#include "vellum/text.h"
#include "vellum/text_book.h"
#include "vellum/touch.h"
#include "vellum/ui.h"
#include "vellum/updates.h"
#include "vellum/upload_page.h"
#include "vellum/xml.h"

using namespace vellum;

static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

namespace {

constexpr int kW = 800;
constexpr int kH = 480;

struct Fixture {
  std::vector<uint8_t> buf = std::vector<uint8_t>(Framebuffer::bytes_for(kW, kH));
  Framebuffer fb{buf.data(), kW, kH};
};

bool physical_black(const Framebuffer& fb, int px, int py) {
  return (fb.data()[static_cast<size_t>(py) * fb.stride() + px / 8] & (0x80 >> (px % 8))) == 0;
}

void test_rotation_mapping() {
  for (Rotation r : {Rotation::R0, Rotation::R90, Rotation::R180, Rotation::R270}) {
    Fixture f;
    f.fb.set_rotation(r);
    f.fb.clear(Color::White);
    f.fb.set_pixel(0, 0, Color::Black);
    CHECK(f.fb.pixel(0, 0) == Color::Black);
    CHECK(f.fb.pixel(1, 0) == Color::White);
    const Rect p = f.fb.to_physical({0, 0, 1, 1});
    CHECK(physical_black(f.fb, p.x, p.y));
  }
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  CHECK(f.fb.width() == kH);
  CHECK(f.fb.height() == kW);
  // Logical top-left lands on the physical top-right in R90.
  CHECK(f.fb.to_physical(Rect{0, 0, 1, 1}) == (Rect{kW - 1, 0, 1, 1}));
}

void test_fill_rect_matches_set_pixel() {
  // Odd offsets exercise the masked edge bytes.
  for (Rotation r : {Rotation::R0, Rotation::R90}) {
    Fixture a, b;
    a.fb.set_rotation(r);
    b.fb.set_rotation(r);
    a.fb.clear(Color::White);
    b.fb.clear(Color::White);
    const Rect rect{3, 5, 37, 11};
    a.fb.fill_rect(rect, Color::Black);
    for (int y = rect.y; y < rect.y + rect.h; ++y)
      for (int x = rect.x; x < rect.x + rect.w; ++x) b.fb.set_pixel(x, y, Color::Black);
    CHECK(a.buf == b.buf);
  }
}

void test_fill_rect_clips() {
  Fixture f;
  f.fb.clear(Color::White);
  f.fb.clear_dirty();
  f.fb.fill_rect({-10, -10, 20, 20}, Color::Black);
  CHECK(f.fb.pixel(9, 9) == Color::Black);
  CHECK(f.fb.pixel(10, 10) == Color::White);
  CHECK(f.fb.dirty() == (Rect{0, 0, 10, 10}));
  f.fb.clear_dirty();
  f.fb.fill_rect({kW + 5, 0, 10, 10}, Color::Black);
  CHECK(f.fb.dirty().empty());
}

class CountingPanel : public DisplayPanel {
 public:
  int width() const override { return kW; }
  int height() const override { return kH; }
  void present(const Framebuffer&, RefreshMode mode, Rect) override { (mode == RefreshMode::Full ? full : fast)++; }
  int fast = 0;
  int full = 0;
};

void test_display_queue_cadence() {
  Fixture f;
  CountingPanel panel;
  DisplayQueue q(panel, f.fb, RefreshPolicy{3});
  f.fb.clear_dirty();
  CHECK(q.submit() == RefreshMode::Fast);  // nothing dirty: nothing sent
  CHECK(panel.fast == 0 && panel.full == 0);

  for (int i = 0; i < 4; ++i) {
    f.fb.set_pixel(i, 0, Color::Black);
    q.submit();
  }
  CHECK(panel.fast == 3);
  CHECK(panel.full == 1);
  CHECK(q.fast_since_full() == 0);
  CHECK(f.fb.dirty().empty());
}

void test_utf8() {
  const std::string_view s = "a\xC4\x85\xE2\x80\x94\xF0\x9F\x93\x96\xFF\xE2\x80";
  size_t i = 0;
  CHECK(decode_utf8(s, i) == 'a');
  CHECK(decode_utf8(s, i) == 0x0105);  // ą
  CHECK(decode_utf8(s, i) == 0x2014);  // em dash
  CHECK(decode_utf8(s, i) == 0x1F4D6);
  CHECK(decode_utf8(s, i) == 0xFFFD);  // stray byte
  CHECK(decode_utf8(s, i) == 0xFFFD);  // truncated sequence
  CHECK(i < s.size());
  while (i < s.size()) decode_utf8(s, i);
  CHECK(i == s.size());
}

void test_font_lookup_and_kerning() {
  const Font& f = fonts::literata_26;
  CHECK(f.glyphs[f.find('A')].codepoint == 'A');
  CHECK(f.glyphs[f.find(0x0117)].codepoint == 0x0117);  // ė, for Lithuanian
  CHECK(f.find(0x4E00) == f.fallback);
  CHECK(f.kern(f.find('A'), f.find('V')) < 0);
  CHECK(text_width(f, "AV") < text_width(f, "A") + text_width(f, "V"));
  CHECK(text_width(f, "") == 0);
}

void test_pagination_covers_text_once() {
  for (const Book* b : builtin_books()) {
    const Book& book = *b;
    const PageStyle style{&fonts::literata_26, {28, 70, 424, 660}, 26, &hyphenation::en_us};
    TextPos pos{};
    int pages = 0;
    while (pos.paragraph < book.paragraph_count && pages < 1000) {
      const TextPos next = typeset_page(style, book.paragraphs, book.paragraph_count, pos, nullptr);
      CHECK(next.paragraph > pos.paragraph || (next.paragraph == pos.paragraph && next.offset > pos.offset));
      pos = next;
      ++pages;
    }
    CHECK(pos == (TextPos{book.paragraph_count, 0}));
    CHECK(pages > 5 && pages < 100);

    // Drawing a page stops at the same place as measuring it, and stays inside its area.
    Fixture f;
    f.fb.set_rotation(Rotation::R90);
    f.fb.clear(Color::White);
    f.fb.clear_dirty();
    const TextPos measured = typeset_page(style, book.paragraphs, book.paragraph_count, {}, nullptr);
    CHECK(typeset_page(style, book.paragraphs, book.paragraph_count, {}, &f.fb) == measured);
    for (int y = 0; y < f.fb.height(); ++y) {
      for (int x = 0; x < f.fb.width(); ++x) {
        const bool inside = x >= style.area.x - 2 && x < style.area.x + style.area.w + 2 && y >= style.area.y &&
                            y < style.area.y + style.area.h;
        if (!inside) CHECK(f.fb.pixel(x, y) == Color::White);
      }
    }
  }
}

void test_library_order() {
  Library lib;
  Epub epub;
  lib.load(builtin_books(), nullptr, epub);
  CHECK(lib.count() == 4);
  CHECK(lib.current() == 0);  // nothing opened: the first book
  int order[Library::kMaxBooks];
  CHECK(lib.by_recent(order, Library::kMaxBooks) == 4);
  CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3);
  lib.touch(2);
  lib.touch(3);
  CHECK(lib.current() == 3);
  lib.by_recent(order, Library::kMaxBooks);
  CHECK(order[0] == 3 && order[1] == 2 && order[2] == 0 && order[3] == 1);
}

void test_book_count_plurals() {
  char out[32];
  const auto lt = [&](int n) {
    format_book_count(Language::Lithuanian, n, out, sizeof(out));
    return std::string(out);
  };
  CHECK(lt(1) == "1 knyga");
  CHECK(lt(4) == "4 knygos");
  CHECK(lt(11) == "11 knygų");
  CHECK(lt(20) == "20 knygų");
  CHECK(lt(21) == "21 knyga");
  CHECK(lt(22) == "22 knygos");
  format_book_count(Language::English, 1, out, sizeof(out));
  CHECK(std::string(out) == "1 book");
}

void test_reading_style_changes_layout() {
  const Book& book = books::alice;
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  const auto pages = [&](const Settings& s) {
    const ReadingStyle style = reading_style(s);
    TextPos pos{};
    int n = 0;
    while (pos.paragraph < book.paragraph_count && n < 1000) {
      pos = typeset_page(page_style(style, f.fb, book.language, n == 0), book.paragraphs, book.paragraph_count, pos,
                         nullptr);
      ++n;
    }
    return n;
  };
  Settings normal;
  Settings big = normal;
  big.text_size = TextSize::ExtraLarge;
  Settings loose = normal;
  loose.line_spacing = Spacing::Wide;
  Settings wide = normal;
  wide.margins = Spacing::Wide;
  CHECK(pages(big) > pages(normal));
  CHECK(pages(loose) > pages(normal));
  CHECK(pages(wide) > pages(normal));
}

void test_interface_text_fits() {
  // Hints are one line between the screen margins in every language.
  for (Language lang : {Language::English, Language::Lithuanian}) {
    const Strings& t = strings(lang);
    for (const char* hint : {t.hint_home,
                             t.hint_list,
                             t.hint_settings,
                             t.hint_transfer,
                             t.transfer_lead,
                             t.hint_battery,
                             t.hint_light,
                             t.hint_check,
                             t.hint_updates,
                             t.hint_setup,
                             t.setup_lead,
                             t.hint_pick_line,
                             t.hint_pick_word,
                             t.store_lead,
                             t.hint_store_busy,
                             t.charge_hint,
                             t.hint_pictures_shuffle,
                             t.hint_pictures_one,
                             t.hint_preview,
                             t.hint_picture}) {
      const int w = text_width_px(fonts::literata_16, hint);
      if (w > kH - 2 * 28) std::printf("too wide (%d px): %s\n", w, hint);
      CHECK(w <= kH - 2 * 28);
    }
    // Software update lines on the Add books screen.
    for (const char* line : {t.updating, t.update_done, t.update_not_vellum, t.update_broken, t.battery_empty}) {
      const int w = text_width_px(fonts::literata_20, line);
      if (w > kH - 2 * 28) std::printf("too wide (%d px): %s\n", w, line);
      CHECK(w <= kH - 2 * 28);
    }
  }
}

std::string read_all(const char* path) {
  std::string out;
  if (std::FILE* f = std::fopen(path, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
  }
  return out;
}

void test_xml_entities() {
  char out[4];
  size_t len = 0;
  size_t pos = 0;
  CHECK(xml::decode_entity("&amp;x", pos, out, len) && len == 1 && out[0] == '&' && pos == 5);
  pos = 0;
  CHECK(xml::decode_entity("&#x2014;", pos, out, len) && len == 3);
  pos = 0;
  CHECK(xml::decode_entity("&shy;", pos, out, len) && len == 0);
  pos = 0;
  CHECK(!xml::decode_entity("&nosuch;", pos, out, len) && pos == 0);
  CHECK(xml::attr(" id=\"c1\" href='a b.xhtml'", "href") == "a b.xhtml");
}

void test_epub() {
  StdioStorage data(VELLUM_TEST_DATA);
  Epub epub;
  CHECK(epub.open(data, "sample.epub"));
  CHECK(std::string(epub.info().title) == "Vellum & the Test Book");
  CHECK(std::string(epub.info().author) == "Arūnas Tester");
  CHECK(std::string(epub.info().language) == "lt");
  CHECK(epub.chapter_count() == 2);

  Chapter ch{};
  CHECK(epub.load_chapter(0, ch));
  const std::vector<std::string> want = {
      "\x01Pirmas skyrius",
      "Labas, pasauli & draugai—sveiki!",
      "Nelaužti tarpai\xC2\xA0čia. Antra eilutė.",
      "Eilėraštis:",
      "pirma eilė",
      "antra eilė",
      "",
      "Paskutinis „sakinys“.",
  };
  CHECK(ch.paragraph_count == want.size());
  for (size_t i = 0; i < want.size() && i < ch.paragraph_count; ++i) {
    if (want[i] != ch.paragraphs[i]) std::printf("paragraph %zu: [%s]\n", i, ch.paragraphs[i]);
    CHECK(want[i] == ch.paragraphs[i]);
  }
  CHECK(epub.load_chapter(1, ch));
  CHECK(ch.paragraph_count == 2 && std::string(ch.paragraphs[0]) == "\x01Second");
  CHECK(!epub.load_chapter(2, ch));
  CHECK(!epub.open(data, "missing.epub"));
}

void test_epub_contents() {
  StdioStorage data(VELLUM_TEST_DATA);
  for (const char* name : {"sample.epub", "sample-ncx.epub"}) {
    Epub epub;
    CHECK(epub.open(data, name));
    CHECK(epub.chapter_count() == 2);
    CHECK(epub.toc_count() == 3);  // the link outside the book is left out
    if (epub.toc_count() != 3) continue;
    CHECK(std::string(epub.toc(0).title) == "Pirmas skyrius");
    CHECK(std::string(epub.toc(1).title).starts_with("Eilėraštis"));
    CHECK(std::string(epub.toc(2).title) == "Second");
    CHECK(epub.toc(0).depth == 0 && epub.toc(1).depth == 1 && epub.toc(2).depth == 0);
    CHECK(epub.toc(0).chapter == 0 && epub.toc(1).chapter == 0 && epub.toc(2).chapter == 1);
    CHECK(epub.toc(0).paragraph == 0 && epub.toc(2).paragraph == 0);
    // The poem's place is known once its chapter has been read.
    CHECK(epub.toc(1).paragraph == TocEntry::kUnknown);
    Chapter ch{};
    CHECK(epub.load_chapter(0, ch));
    CHECK(epub.toc(1).paragraph == 3);
    CHECK(std::string(ch.paragraphs[3]) == "Eilėraštis:");
  }
  CHECK(std::string(strings(Language::English).chapter_n).find("%d") != std::string::npos);
}

void test_card_library_and_progress() {
  // A fresh card folder with one book on it.
  StdioStorage card("test-card");
  std::remove("test-card/vellum/progress.txt");
  std::remove("test-card/vellum/library.txt");
  std::remove("test-card/vellum/settings.txt");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  CHECK(!book.empty());
  CHECK(card.write("books/sample.epub", book.data(), static_cast<uint32_t>(book.size())));

  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  CHECK(lib.count() == 5);
  int found = -1;
  for (int i = 0; i < lib.count(); ++i) {
    if (std::string(lib.entry(i).title) == "Vellum & the Test Book") found = i;
  }
  CHECK(found >= 0);
  if (found < 0) return;
  CHECK(read_all("test-card/vellum/library.txt").find("books/sample.epub") != std::string::npos);

  lib.touch(found);
  lib.state(found).position = {1, {3, 7}};
  lib.state(found).percent = 42;
  CHECK(lib.save_progress(card));

  Library again;
  again.load(builtin_books(), &card, epub);
  CHECK(again.current() == found);
  const BookState& s = again.state(found);
  CHECK(s.position.chapter == 1 && s.position.text.paragraph == 3 && s.position.text.offset == 7);
  CHECK(s.percent == 42);

  Settings settings;
  settings.language = Language::Lithuanian;
  settings.text_size = TextSize::Large;
  settings.justify = false;
  settings.footer = FooterInfo::ChapterTime;
  CHECK(save_settings(card, settings));
  Settings loaded;
  load_settings(card, loaded);
  CHECK(loaded == settings);
}

void test_reader_crosses_chapters() {
  StdioStorage data(VELLUM_TEST_DATA);
  Epub epub;
  CHECK(epub.open(data, "sample.epub"));
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  Reader reader;
  CHECK(reader.open(epub, 1, reading_style(Settings{}), {}, f.fb));
  CHECK(reader.position().chapter == 0);
  int turns = 0;
  while (reader.next()) ++turns;
  CHECK(reader.position().chapter == 1);
  CHECK(reader.percent() == 100);
  while (reader.prev()) --turns;
  CHECK(turns == 0 && reader.position().chapter == 0);
}

void test_time_format() {
  char out[16];
  format_time({2026, 10, 4, 14, 5, 0}, TimeFormat::H24, out, sizeof(out));
  CHECK(std::string(out) == "14:05");
  format_time({2026, 10, 4, 14, 5, 0}, TimeFormat::H12, out, sizeof(out));
  CHECK(std::string(out) == "2:05 PM");
  format_time({2026, 10, 4, 0, 30, 0}, TimeFormat::H12, out, sizeof(out));
  CHECK(std::string(out) == "12:30 AM");
  format_time({2026, 10, 4, 12, 0, 0}, TimeFormat::H12, out, sizeof(out));
  CHECK(std::string(out) == "12:00 PM");
}

std::string hyphenated(const Hyphenator& h, std::string_view word) {
  uint16_t breaks[16];
  const int n = hyphenate(h, word, breaks, 16);
  std::string out;
  size_t last = 0;
  for (int i = 0; i < n; ++i) {
    out.append(word.substr(last, breaks[i] - last)).append("-");
    last = breaks[i];
  }
  return out.append(word.substr(last));
}

void test_hyphenation() {
  CHECK(hyphenated(hyphenation::en_us, "hyphenation") == "hy-phen-ation");
  CHECK(hyphenated(hyphenation::en_us, "Considering,") == "Con-sid-er-ing,");
  CHECK(hyphenated(hyphenation::en_us, "associate") == "as-so-ciate");                  // exception list
  CHECK(hyphenated(hyphenation::en_us, "\xE2\x80\x98table") == "\xE2\x80\x98ta-ble");   // leading quote
  CHECK(hyphenated(hyphenation::en_us, "they\xE2\x80\x99ll") == "they\xE2\x80\x99ll");  // apostrophe: left alone
  CHECK(hyphenated(hyphenation::en_us, "cat") == "cat");
  CHECK(hyphenated(hyphenation::lt, "Lietuva") == "Lie-tu-va");
  CHECK(hyphenated(hyphenation::lt, "\xC5\xA0VIESA") == "\xC5\xA0VIE-SA");  // ŠVIESA, uppercase
  CHECK(hyphenator_for("en-GB") == &hyphenation::en_us);
  CHECK(hyphenator_for("lt") == &hyphenation::lt);
  CHECK(hyphenator_for("de") == nullptr);
}

// Liang's method the plain way: every piece of ".word." looked up whole.
// hyphenate() skips pieces no pattern starts with; the breaks must not change.
std::vector<uint16_t> reference_breaks(const Hyphenator& h, std::string_view word) {
  std::string key = ".";
  std::vector<size_t> unit{0};   // where each unit (dot or letter) starts in key
  std::vector<uint16_t> starts;  // where each letter starts in word
  for (size_t i = 0; i < word.size();) {
    starts.push_back(static_cast<uint16_t>(i));
    const size_t at = i;
    decode_utf8(word, i);
    unit.push_back(key.size());
    key.append(word.substr(at, i - at));
  }
  unit.push_back(key.size());
  key += '.';
  unit.push_back(key.size());
  const int n = static_cast<int>(starts.size());
  std::vector<uint8_t> values(n + 3, 0);
  for (int a = 0; a < n + 2; ++a) {
    for (int b = a + 1; b <= n + 2; ++b) {
      const std::string_view part(key.data() + unit[a], unit[b] - unit[a]);
      const HyphenPattern* end = h.patterns + h.pattern_count;
      const HyphenPattern* p = std::lower_bound(h.patterns, end, part, [](const HyphenPattern& x, std::string_view k) {
        return std::string_view(x.letters) < k;
      });
      if (p == end || std::string_view(p->letters) != part) continue;
      for (int g = 0; g <= b - a; ++g) values[a + g] = std::max<uint8_t>(values[a + g], p->digits[g] - '0');
    }
  }
  std::vector<uint16_t> out;
  for (int g = 1 + h.left_min; g <= 1 + n - h.right_min; ++g) {
    if (values[g] & 1) out.push_back(starts[g - 1]);
  }
  return out;
}

void test_hyphenation_matches_reference() {
  // Every lowercase word of the built-in books, plus some Lithuanian.
  std::vector<std::string> words;
  for (const Book* b : builtin_books()) {
    for (uint32_t i = 0; i < b->paragraph_count; ++i) {
      std::string w;
      for (const char* c = b->paragraphs[i];; ++c) {
        if (*c >= 'a' && *c <= 'z') {
          w += *c;
          continue;
        }
        if (w.size() >= 4) words.push_back(w);
        w.clear();
        if (*c == '\0') break;
      }
    }
  }
  CHECK(words.size() > 5000);
  int compared = 0;
  for (const std::string& w : words) {
    bool exception = false;  // the exception list wins over patterns
    for (uint32_t e = 0; e < hyphenation::en_us.exception_count; ++e) {
      std::string bare = hyphenation::en_us.exceptions[e];
      std::erase(bare, '-');
      exception = exception || bare == w;
    }
    if (exception) continue;
    uint16_t breaks[24];
    const int n = hyphenate(hyphenation::en_us, w, breaks, 24);
    CHECK(std::vector<uint16_t>(breaks, breaks + n) == reference_breaks(hyphenation::en_us, w));
    ++compared;
  }
  CHECK(compared > 5000);
  for (const char* w : {"lietuva", "\xc5\xa1viesa", "knygos", "skaitykl\xc4\x97", "vakarien\xc4\x97", "\xc5\xbeodynas",
                        "pasaulis", "u\xc5\xbeuolaida", "nepriklausomyb\xc4\x97"}) {
    uint16_t breaks[24];
    const int n = hyphenate(hyphenation::lt, w, breaks, 24);
    CHECK(std::vector<uint16_t>(breaks, breaks + n) == reference_breaks(hyphenation::lt, w));
  }
}

void test_rotation_round_trip() {
  for (Rotation r : {Rotation::R0, Rotation::R90, Rotation::R180, Rotation::R270}) {
    Fixture f;
    f.fb.set_rotation(r);
    for (auto [x, y] : {std::pair{0, 0}, {5, 9}, {f.fb.width() - 1, f.fb.height() - 1}}) {
      int px, py, lx, ly;
      f.fb.to_physical(x, y, px, py);
      CHECK(px >= 0 && px < kW && py >= 0 && py < kH);
      f.fb.to_logical(px, py, lx, ly);
      CHECK(lx == x && ly == y);
    }
  }
}

// A touch event at a logical point on an R90 (portrait) screen.
InputEvent touch_at(InputEvent::Type type, int x, int y) {
  InputEvent ev;
  ev.type = type;
  int px, py;
  rotate_to_physical(Rotation::R90, kW, kH, x, y, px, py);
  ev.x = static_cast<int16_t>(px);
  ev.y = static_cast<int16_t>(py);
  return ev;
}

Gesture stroke(TouchTracker& t, const Framebuffer& fb, int x0, int y0, int x1, int y1) {
  using T = InputEvent::Type;
  t.feed(touch_at(T::TouchDown, x0, y0), fb);
  t.feed(touch_at(T::TouchMove, (x0 + x1) / 2, (y0 + y1) / 2), fb);
  return t.feed(touch_at(T::TouchUp, x1, y1), fb);
}

void test_gestures() {
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  TouchTracker t;
  Gesture g = stroke(t, f.fb, 100, 200, 108, 195);
  CHECK(g.kind == Gesture::Kind::Tap && g.x == 100 && g.y == 200);
  CHECK(stroke(t, f.fb, 400, 300, 150, 320).kind == Gesture::Kind::SwipeLeft);
  CHECK(stroke(t, f.fb, 100, 300, 350, 280).kind == Gesture::Kind::SwipeRight);
  CHECK(stroke(t, f.fb, 240, 600, 250, 200).kind == Gesture::Kind::SwipeUp);
  CHECK(stroke(t, f.fb, 240, 200, 230, 600).kind == Gesture::Kind::SwipeDown);
  CHECK(stroke(t, f.fb, 100, 100, 200, 200).kind == Gesture::Kind::None);  // diagonal
  // A wander that comes back is not a tap.
  using T = InputEvent::Type;
  t.feed(touch_at(T::TouchDown, 100, 100), f.fb);
  t.feed(touch_at(T::TouchMove, 200, 100), f.fb);
  CHECK(t.feed(touch_at(T::TouchUp, 100, 100), f.fb).kind == Gesture::Kind::None);
  // A release with no press does nothing.
  CHECK(t.feed(touch_at(T::TouchUp, 100, 100), f.fb).kind == Gesture::Kind::None);
}

class NoInput : public InputSource {
 public:
  bool wait(InputEvent&, uint32_t) override { return false; }
};

class FixedPower : public Power {
 public:
  int battery_percent() override { return 80; }
  bool charging() override { return false; }
};

void tap(App& app, int x, int y) {
  app.handle(touch_at(InputEvent::Type::TouchDown, x, y));
  app.handle(touch_at(InputEvent::Type::TouchUp, x, y));
}

void swipe(App& app, int x0, int y0, int x1, int y1) {
  app.handle(touch_at(InputEvent::Type::TouchDown, x0, y0));
  app.handle(touch_at(InputEvent::Type::TouchMove, x1, y1));
  app.handle(touch_at(InputEvent::Type::TouchUp, x1, y1));
}

void test_app_touch() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  Board board{"test", panel, input, power, nullptr, nullptr, nullptr, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  CHECK(panel.full == 1);

  tap(*app, 10, 790);  // empty space at the bottom: nothing happens
  CHECK(panel.full == 1 && panel.fast == 0);

  tap(*app, 240, 200);  // the current book's card opens it
  CHECK(panel.full == 2);

  tap(*app, 50, 400);  // left third on the first page: no page before it
  CHECK(panel.fast == 0);
  tap(*app, 400, 400);  // forward
  CHECK(panel.fast == 1);
  swipe(*app, 400, 400, 100, 410);  // swipe left: forward
  CHECK(panel.fast == 2);
  swipe(*app, 100, 400, 400, 410);  // swipe right: back
  tap(*app, 50, 400);               // back again, to the first page
  CHECK(panel.fast == 4);
  tap(*app, 50, 400);
  CHECK(panel.fast == 4);
  delete app;
}

std::string safe(const char* in) {
  char out[160];
  return safe_book_name(in, out, sizeof(out)) ? std::string(out) : std::string("<rejected>");
}

void test_upload_helpers() {
  char buf[64];
  CHECK(url_decode("Ana%C3%AFs+Nin.epub", 19, buf, sizeof(buf)) && std::string(buf) == "Ana\xC3\xAFs Nin.epub");
  CHECK(!url_decode("bad%4", 5, buf, sizeof(buf)));
  CHECK(!url_decode("bad%zz", 6, buf, sizeof(buf)));
  CHECK(!url_decode("toolong", 7, buf, 4));

  CHECK(safe("Moby Dick.epub") == "Moby Dick.epub");
  CHECK(safe("C:\\fakepath\\Book.EPUB") == "Book.EPUB");
  CHECK(safe("../../vellum/settings.epub") == "settings.epub");
  CHECK(safe("..hidden.epub") == "hidden.epub");
  CHECK(safe("What? Why: \"Now\".epub") == "What_ Why_ _Now_.epub");
  CHECK(safe("notes.pdf") == "<rejected>");
  CHECK(safe(".epub") == "<rejected>");
  CHECK(safe("") == "<rejected>");
  CHECK(safe_picture_name("C:\\fakepath\\Sea.JPG", buf, sizeof(buf)) && std::string(buf) == "Sea.JPG");
  CHECK(safe_picture_name("../dawn.jpeg", buf, sizeof(buf)) && std::string(buf) == "dawn.jpeg");
  CHECK(safe_picture_name("a.bmp", buf, sizeof(buf)) && safe_picture_name("b.png", buf, sizeof(buf)));
  CHECK(!safe_picture_name("cover.gif", buf, sizeof(buf)) && !safe_picture_name("book.epub", buf, sizeof(buf)));
  // Long names keep their extension and stay whole UTF-8.
  std::string long_name;
  for (int i = 0; i < 80; ++i) long_name += "\xC4\x85";  // ą, two bytes each
  long_name += ".epub";
  const std::string cut = safe(long_name.c_str());
  CHECK(cut.size() <= 120 && cut.size() > 100);
  CHECK(cut.substr(cut.size() - 5) == ".epub");
  CHECK((cut.size() - 5) % 2 == 0);

  char json[32];
  size_t len = 0;
  CHECK(json_escape("a\"b\\c\n", json, sizeof(json), len) && std::string(json) == "a\\\"b\\\\c\\u000a");
  len = 0;
  CHECK(!json_escape("this will not fit in the buffer at all", json, sizeof(json), len));
}

void test_upload_page_embedded() {
  const std::string page = read_all(VELLUM_TEST_DATA "/../../components/vellum_core/web/upload.html");
  CHECK(!page.empty());
  CHECK(page == std::string(kUploadPage, kUploadPageSize));  // run tools/embed_page.py after editing the page
  const std::string setup = read_all(VELLUM_TEST_DATA "/../../components/vellum_core/web/setup.html");
  CHECK(!setup.empty());
  CHECK(setup == std::string(kSetupPage, kSetupPageSize));
}

class FakeTransfer : public Transfer {
 public:
  bool start(TransferInfo& info) override {
    ++starts;
    std::snprintf(info.network, sizeof(info.network), "Vellum-TEST");
    std::snprintf(info.password, sizeof(info.password), "12345678");
    std::snprintf(info.address, sizeof(info.address), "http://192.168.4.1");
    return true;
  }
  void stop() override { ++stops; }
  void status(TransferStatus& out) override { out = st; }
  TransferStatus st;
  int starts = 0;
  int stops = 0;
};

void test_app_transfer() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  std::remove("test-card/vellum/state.txt");  // start on the home screen
  StdioStorage card("test-card");
  FakeTransfer transfer;
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data(), &transfer};
  auto* app = new App(board);
  app->start();

  InputEvent ev;
  ev.type = InputEvent::Type::Transfer;
  const int fast = panel.fast;
  app->handle(ev);  // not on the Add books screen: ignored
  CHECK(panel.fast == fast);

  tap(*app, 240, 640);  // "Add books"
  CHECK(transfer.starts == 1);
  transfer.st.received = 1;
  std::snprintf(transfer.st.last, sizeof(transfer.st.last), "New.epub");
  app->handle(ev);  // a book arrived: the screen updates
  CHECK(panel.fast > fast);

  InputEvent home;
  home.type = InputEvent::Type::KeyDown;
  home.key = Key::Home;
  app->handle(home);
  CHECK(transfer.stops == 1);
  delete app;
}

class RestartingPower : public FixedPower {
 public:
  void restart() override { ++restarts; }
  int restarts = 0;
};

void test_app_update() {
  using U = TransferStatus::Update;
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  RestartingPower power;
  std::remove("test-card/vellum/state.txt");
  StdioStorage card("test-card");
  FakeTransfer transfer;
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data(), &transfer};
  board.version = "0.1.42";
  auto* app = new App(board);
  app->start();
  tap(*app, 240, 700);  // Settings
  tap(*app, 240, 394);  // Updates
  tap(*app, 240, 475);  // Update from a file: the phone page
  CHECK(transfer.starts == 1);

  InputEvent ev;
  ev.type = InputEvent::Type::Transfer;
  transfer.st.update = U::Receiving;
  transfer.st.update_percent = 40;
  int fast = panel.fast;
  app->handle(ev);  // progress shows, nothing restarts
  CHECK(panel.fast > fast && power.restarts == 0 && transfer.stops == 0);

  transfer.st.update = U::NotVellum;
  fast = panel.fast;
  app->handle(ev);  // a wrong file: says so, carries on
  CHECK(panel.fast > fast && power.restarts == 0 && transfer.stops == 0);

  transfer.st.update = U::Installed;
  const int full = panel.full;
  app->handle(ev);  // a full-screen notice, the radio off, then the restart
  CHECK(panel.full > full);
  CHECK(transfer.stops == 1 && power.restarts == 1);
  CHECK(read_all("test-card/vellum/state.txt") == "home\n");

  app->handle(ev);  // the simulators carry on at home; later events are ignored
  CHECK(power.restarts == 1);
  delete app;
}

class SleepyPower : public Power {
 public:
  int battery_percent() override { return 50; }
  bool charging() override { return false; }
  void sleep() override { ++sleeps; }
  int sleeps = 0;
};

InputEvent key_event(InputEvent::Type type, Key key) {
  InputEvent ev;
  ev.type = type;
  ev.key = key;
  return ev;
}

void test_app_sleep() {
  using T = InputEvent::Type;
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  SleepyPower power;
  std::remove("test-card/vellum/state.txt");
  StdioStorage card("test-card");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  {
    auto* app = new App(board);
    app->start();
    // A short Power press opens the current book (on release).
    app->handle(key_event(T::KeyDown, Key::Power));
    CHECK(panel.full == 1);
    CHECK(app->wait_ms() == 1000);  // waiting to see if Power is held
    app->handle(key_event(T::KeyUp, Key::Power));
    CHECK(panel.full == 2);
    CHECK(app->wait_ms() == 10 * 60 * 1000);  // default: sleep after 10 idle minutes
    app->handle(key_event(T::KeyDown, Key::Next));

    // Holding Power sleeps; its release does nothing more.
    app->handle(key_event(T::KeyDown, Key::Power));
    app->timeout();
    CHECK(power.sleeps == 1);
    CHECK(panel.full == 3);  // the sleep screen
    app->handle(key_event(T::KeyUp, Key::Power));
    CHECK(panel.full == 3);
    CHECK(read_all("test-card/vellum/state.txt") == "reader\n");

    // Any button wakes it, back in the book.
    app->handle(key_event(T::KeyDown, Key::Next));
    CHECK(panel.full == 4);
    // Idle long enough: sleeps again.
    app->timeout();
    CHECK(power.sleeps == 2);
    delete app;
  }
  {
    // A restart after sleep opens the book again.
    const int full = panel.full;
    auto* app = new App(board);
    app->start();
    CHECK(panel.full == full + 1);
    const int fast = panel.fast;
    app->handle(key_event(T::KeyDown, Key::Prev));  // back a page: only works in the reader
    CHECK(panel.fast == fast + 1);
    delete app;
  }
  std::remove("test-card/vellum/state.txt");
}

void press(App& app, Key key) {
  app.handle(key_event(InputEvent::Type::KeyDown, key));
  app.handle(key_event(InputEvent::Type::KeyUp, key));
}

int count_lines(const std::string& s) { return static_cast<int>(std::count(s.begin(), s.end(), '\n')); }

void test_app_contents_and_bookmarks() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  SleepyPower power;
  StdioStorage card("test-card");
  std::remove("test-card/vellum/state.txt");
  std::remove("test-card/vellum/bookmarks.txt");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  CHECK(card.write("books/sample.epub", book.data(), static_cast<uint32_t>(book.size())));
  const char* progress = "books/sample.epub\t0\t0\t0\t0\t1\n";  // the book read last
  CHECK(card.write("vellum/progress.txt", progress, static_cast<uint32_t>(std::strlen(progress))));
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);  // open the book
  auto progress_has = [](const char* fields) {
    return read_all("test-card/vellum/progress.txt").find(fields) != std::string::npos;
  };
  CHECK(progress_has("books/sample.epub\t0\t0\t0\t"));

  // Rows: back, bookmark this page, CHAPTERS, the three contents entries.
  int full = panel.full;
  press(*app, Key::Power);  // contents
  CHECK(panel.full == full + 1);
  press(*app, Key::Next);
  press(*app, Key::Power);  // bookmark this page; back in the book
  const std::string marks = read_all("test-card/vellum/bookmarks.txt");
  CHECK(count_lines(marks) == 1 && marks.starts_with("books/sample.epub\t0\t0\t0\t"));
  CHECK(marks.find("\tLabas, pasauli") != std::string::npos);  // the page's first words, after the heading

  // Rows now: back, remove, light, look up, turn pages, add to shelf, BOOKMARKS, the bookmark, CHAPTERS,
  // three entries.
  press(*app, Key::Power);
  for (int i = 0; i < 9; ++i) press(*app, Key::Next);  // labels are skipped
  press(*app, Key::Power);                             // "Second"
  CHECK(progress_has("books/sample.epub\t1\t0\t0\t"));

  press(*app, Key::Power);
  press(*app, Key::Next);
  press(*app, Key::Power);  // bookmark this page too
  CHECK(count_lines(read_all("test-card/vellum/bookmarks.txt")) == 2);

  // Back to the first bookmark (the bookmarks come in reading order), then remove it.
  press(*app, Key::Power);
  for (int i = 0; i < 6; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(progress_has("books/sample.epub\t0\t0\t0\t"));
  press(*app, Key::Power);
  press(*app, Key::Next);
  press(*app, Key::Power);  // remove this bookmark
  const std::string left = read_all("test-card/vellum/bookmarks.txt");
  CHECK(count_lines(left) == 1 && left.starts_with("books/sample.epub\t1\t"));

  // Prev from the top wraps to the last chapter; Home leaves for the home screen.
  press(*app, Key::Power);
  press(*app, Key::Prev);
  press(*app, Key::Power);
  CHECK(progress_has("books/sample.epub\t1\t0\t0\t"));
  delete app;

  // Bookmarks survive a restart.
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  int found = -1;
  for (int i = 0; i < lib.count(); ++i) {
    if (std::string(lib.entry(i).path) == "books/sample.epub") found = i;
  }
  CHECK(found >= 0);
  int marks_out[4];
  CHECK(found >= 0 && lib.bookmarks_of(found, marks_out, 4) == 1);
  std::remove("test-card/vellum/bookmarks.txt");
  std::remove("test-card/vellum/state.txt");
}

void test_dates_and_battery_log() {
  CHECK(seconds_since_2000({2000, 1, 1, 0, 0, 0}) == 0);
  CHECK(seconds_since_2000({2000, 1, 2, 0, 0, 1}) == 86401);
  CHECK(seconds_since_2000({2026, 10, 4, 14, 5, 0}) == 844437900u);
  for (uint32_t days : {0u, 59u, 60u, 365u, 9773u, 36524u}) {
    uint16_t y;
    uint8_t m, d;
    date_from_days(days, y, m, d);
    CHECK(seconds_since_2000({y, m, d, 0, 0, 0}) == days * 86400);
  }
  uint16_t y;
  uint8_t m, d;
  date_from_days(60, y, m, d);
  CHECK(y == 2000 && m == 3 && d == 1);  // 2000 is a leap year

  DateTime t{};
  CHECK(parse_datetime("2026-10-04 14:05:09", 19, t));
  CHECK(t.year == 2026 && t.month == 10 && t.day == 4 && t.hour == 14 && t.minute == 5 && t.second == 9);
  for (const char* bad : {"2026-13-04 14:05:09", "2026-02-29 10:00:00", "1999-10-04 14:05:09", "2026-10-04T14:05:09",
                          "2026-10-04 24:00:00", "2026-1-04 14:05:090"}) {
    CHECK(!parse_datetime(bad, std::strlen(bad), t));
  }
  CHECK(parse_datetime("2024-02-29 23:59:59", 19, t));

  // Two hours of reading from 100% to 90%, then a night asleep losing 2%.
  BatteryLog log;
  using K = BatterySample::Kind;
  const uint32_t t0 = 1000000;
  log.add({t0, 100, K::Wake, false});
  for (int i = 1; i <= 12; ++i) {
    log.add({t0 + i * 600u, static_cast<uint8_t>(100 - (i * 10) / 12), K::Awake, false});
  }
  CHECK(log.estimate().awake_tenths_per_hour == 50);  // 5% an hour
  CHECK(log.estimate().asleep_tenths_per_day == -1);  // no night yet
  log.add({t0 + 7200, 90, K::Sleep, false});
  log.add({t0 + 7200 + 12 * 3600, 88, K::Wake, false});
  CHECK(log.count() == 15);
  BatteryEstimate e = log.estimate();
  CHECK(e.awake_tenths_per_hour == 50);
  CHECK(e.asleep_tenths_per_day == 40);  // 2% in 12 hours
  CHECK(e.hours_left == 88 * 14 / 12);   // 12% in 14 hours
  // Charging stretches do not count.
  log.add({t0 + 60000, 95, K::Awake, true});
  log.add({t0 + 60600, 100, K::Awake, false});
  CHECK(log.estimate().awake_tenths_per_hour == 50);

  // Saved and read back the same.
  StdioStorage card("test-card");
  CHECK(log.save(card));
  BatteryLog again;
  again.load(card);
  CHECK(again.count() == log.count());
  CHECK(again.latest()->time == t0 + 60600 && again.latest()->percent == 100 && !again.latest()->charging);
  CHECK(again.at(13).kind == K::Sleep && again.at(15).charging);
  // The clock set back an hour drops what now lies in the future.
  again.add({t0 + 60600 - 3600, 99, K::Awake, false});
  CHECK(again.count() == log.count() - 1);
  std::remove("test-card/vellum/battery.txt");

  CHECK(std::string(strings(Language::English).per_hour).find("%s") != std::string::npos);
  char out[32];
  format_time_left(Language::Lithuanian, 72, out, sizeof(out));
  CHECK(std::string(out) == "Apie 3 dienas");
  format_time_left(Language::English, 5, out, sizeof(out));
  CHECK(std::string(out) == "About 5 hours");
  format_tenths(Language::Lithuanian, 42, out, sizeof(out));
  CHECK(std::string(out) == "4,2");
}

class TestClock : public Clock {
 public:
  bool now(DateTime& out) override {
    if (!known) return false;
    uint16_t y;
    uint8_t m, d;
    date_from_days(seconds / 86400, y, m, d);
    const uint32_t s = seconds % 86400;
    out = {y, m, d, static_cast<uint8_t>(s / 3600), static_cast<uint8_t>(s / 60 % 60), static_cast<uint8_t>(s % 60)};
    return true;
  }
  bool known = true;
  uint32_t seconds = 844437900u;
};

class DrainingPower : public Power {
 public:
  int battery_percent() override { return percent; }
  bool charging() override { return plugged; }
  int percent = 80;
  bool plugged = false;
};

void test_app_battery_log() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  DrainingPower power;
  TestClock clock;
  std::remove("test-card/vellum/battery.txt");
  std::remove("test-card/vellum/state.txt");
  StdioStorage card("test-card");
  Board board{"test", panel, input, power, nullptr, &clock, &card, Rotation::R90, buf.data()};
  auto lines = [] {
    const std::string s = read_all("test-card/vellum/battery.txt");
    return static_cast<int>(std::count(s.begin(), s.end(), '\n'));
  };
  {
    App app(board);
    app.start();
    CHECK(lines() == 1);  // started: written at once
    press(app, Key::Next);
    CHECK(lines() == 1);  // too soon for another reading
    clock.seconds += 600;
    power.percent = 79;
    press(app, Key::Next);  // read now, written within the hour
    clock.seconds += 600;
    power.plugged = true;
    press(app, Key::Next);  // plugging in: read and written now
    CHECK(lines() == 3);
    power.plugged = false;
    // Sleeping writes everything, then the screen shows the battery screen.
    app.handle(key_event(InputEvent::Type::KeyDown, Key::Power));
    app.timeout();
    CHECK(lines() == 5);  // the unplug and the sleep
    CHECK(read_all("test-card/vellum/battery.txt").find("\ts\t") != std::string::npos);
  }
  {
    // A restart carries on from the history on the card.
    clock.seconds += 3600;
    App app(board);
    app.start();
    CHECK(lines() == 6);
  }
  {
    // Without a clock nothing is recorded, and the battery screen says why.
    clock.known = false;
    std::remove("test-card/vellum/battery.txt");
    App app(board);
    app.start();
    CHECK(lines() == 0);
  }
  std::remove("test-card/vellum/battery.txt");
  std::remove("test-card/vellum/state.txt");
}

class RecordingLight : public Frontlight {
 public:
  void set(uint8_t l, uint8_t w) override {
    level = l;
    warmth = w;
  }
  int level = -1;
  int warmth = -1;
};

void test_app_front_light() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  SleepyPower power;
  RecordingLight light;
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/state.txt");
  StdioStorage card("test-card");
  Board board{"test", panel, input, power, &light, nullptr, &card, Rotation::R90, buf.data()};
  {
    App app(board);
    app.start();
    CHECK(light.level == 0);  // off until asked for
    press(app, Key::Power);   // open the current book
    press(app, Key::Power);   // contents
    press(app, Key::Next);
    press(app, Key::Next);
    press(app, Key::Power);  // front light
    for (int i = 0; i < 4; ++i) press(app, Key::Next);
    CHECK(light.level == 4 * 255 / 10);  // applied at once
    CHECK(light.warmth == 3 * 255 / 10);
    press(app, Key::Power);  // warmth
    for (int i = 0; i < 5; ++i) press(app, Key::Prev);
    CHECK(light.warmth == 0 && light.level == 102);
    press(app, Key::Power);  // done
    press(app, Key::Power);  // back in the book, settings saved
    const std::string saved = read_all("test-card/vellum/settings.txt");
    CHECK(saved.find("light=4\n") != std::string::npos && saved.find("warmth=0\n") != std::string::npos);

    // Off while asleep, back on waking.
    app.handle(key_event(InputEvent::Type::KeyDown, Key::Power));
    app.timeout();
    CHECK(light.level == 0);
    app.handle(key_event(InputEvent::Type::KeyUp, Key::Power));
    press(app, Key::Next);
    CHECK(light.level == 102);
  }
  {
    // A restart turns it on as it was left.
    light.level = -1;
    App app(board);
    app.start();
    CHECK(light.level == 102 && light.warmth == 0);
  }
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/state.txt");
}

// ---- Plain text, FictionBook and web-page books ------------------------------

void test_app_opens_fb2() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  SleepyPower power;
  StdioStorage card("test-card-app-fb2");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.fb2");
  CHECK(card.write("books/sample.fb2", book.data(), static_cast<uint32_t>(book.size())));
  const char* progress = "books/sample.fb2\t0\t0\t0\t0\t1\n";  // the book read last
  CHECK(card.write("vellum/progress.txt", progress, static_cast<uint32_t>(std::strlen(progress))));
  std::remove("test-card-app-fb2/vellum/library.txt");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);  // open the book
  auto progress_has = [](const char* fields) {
    return read_all("test-card-app-fb2/vellum/progress.txt").find(fields) != std::string::npos;
  };
  CHECK(progress_has("books/sample.fb2\t0\t0\t0\t"));
  // Contents: back, bookmark, light, look up, turn pages, add to shelf, CHAPTERS, then the book's five entries.
  press(*app, Key::Power);
  for (int i = 0; i < 9; ++i) press(*app, Key::Next);
  press(*app, Key::Power);  // "Antras skyrius", the fifth chapter
  CHECK(progress_has("books/sample.fb2\t4\t0\t0\t"));
  delete app;
}

void test_charset_detection() {
  using charset::Encoding;
  uint32_t bom = 0;
  auto detect = [&](const std::string& s) {
    return charset::detect(reinterpret_cast<const uint8_t*>(s.data()), static_cast<uint32_t>(s.size()), bom);
  };
  CHECK(detect("plain ASCII text") == Encoding::Utf8 && bom == 0);
  CHECK(detect("\xEF\xBB\xBFLabas") == Encoding::Utf8 && bom == 3);
  CHECK(detect("\xFF\xFEH\0i\0") == Encoding::Utf16LE && bom == 2);
  CHECK(detect(std::string("\xFE\xFF\0H\0i", 6)) == Encoding::Utf16BE && bom == 2);
  CHECK(detect("Žmonės ėjo į turgų") == Encoding::Utf8);
  // UTF-16 without a mark: zeros in every other byte.
  CHECK(detect(std::string("H\0e\0l\0l\0o\0 \0w\0o\0r\0l\0d\0!\0", 24)) == Encoding::Utf16LE);
  // Lithuanian in Windows-1257: š ž ė are bytes F0 FE EB.
  const std::string lt1257 = "\xDEmon\xEBs \xEBjo \xE1 turg\xF8 pirkti \xF0vie\xFEios duonos, \xE8ia \xE0\xFEuolas.";
  CHECK(detect(lt1257) == Encoding::Windows1257);
  char out[256];
  uint32_t n = charset::to_utf8(Encoding::Windows1257, reinterpret_cast<const uint8_t*>(lt1257.data()),
                                static_cast<uint32_t>(lt1257.size()), out, sizeof(out));
  CHECK(std::string(out, n) == "Žmonės ėjo į turgų pirkti šviežios duonos, čia ąžuolas.");
  // The same letters with ISO-8859-13 quotes („ is A5, ” is A1) and no C1 bytes.
  CHECK(detect("\xA5\xD0\xE1 \xF0\xE1\xE0 \xFEod\xEB\xA1 \xEBjo \xF0ok\xF8") == Encoding::Iso8859_13);
  // French in Windows-1252.
  const std::string fr = "Caf\xE9 cr\xE8me, na\xEFve \x93quote\x94 \xE0 la fen\xEAtre.";
  CHECK(detect(fr) == Encoding::Windows1252);
  n = charset::to_utf8(Encoding::Windows1252, reinterpret_cast<const uint8_t*>(fr.data()),
                       static_cast<uint32_t>(fr.size()), out, sizeof(out));
  CHECK(std::string(out, n) == "Café crème, naïve “quote” à la fenêtre.");
  // UTF-16: a surrogate pair, and malformed UTF-8 as U+FFFD.
  const uint8_t u16[] = {'A', 0, 0x3D, 0xD8, 0x00, 0xDE};  // A, U+1F600
  n = charset::to_utf8(Encoding::Utf16LE, u16, sizeof(u16), out, sizeof(out));
  CHECK(std::string(out, n) == "A\xF0\x9F\x98\x80");
  n = charset::to_utf8(Encoding::Utf8, reinterpret_cast<const uint8_t*>("a\xFFz"), 3, out, sizeof(out));
  CHECK(std::string(out, n) == "a\xEF\xBF\xBDz");
  Encoding e;
  CHECK(charset::from_name("Windows-1257", e) && e == Encoding::Windows1257);
  CHECK(charset::from_name("utf-8", e) && e == Encoding::Utf8);
  CHECK(charset::from_name("CP1251", e) && e == Encoding::Windows1251);
  CHECK(!charset::from_name("koi8-r", e));
}

void test_text_headings() {
  for (const char* yes : {"Chapter 1",
                          "CHAPTER IV.",
                          "Chapter 12. The Return",
                          "CHAPTER I. Down the Rabbit-Hole",
                          "Part Two",
                          "BOOK III",
                          "Skyrius 3",
                          "1 skyrius",
                          "I SKYRIUS",
                          "Pirmas skyrius",
                          "Antroji dalis",
                          "I",
                          "XII",
                          "IV.",
                          "12",
                          "Prologue",
                          "EPILOGAS",
                          "Įžanga",
                          "IV. The Return",
                          "  Chapter 7  "}) {
    if (!TextBook::is_heading(yes)) std::printf("not a heading: [%s]\n", yes);
    CHECK(TextBook::is_heading(yes));
  }
  for (const char* no : {"", "It was a dark and stormy night.", "Chapter one was the hardest to write.", "Mix",
                         "1234567", "Part of the plan was simple.", "I. went home", "The Prologue",
                         "Prologue to the story of how it all began, told by the one who saw it"}) {
    if (TextBook::is_heading(no)) std::printf("a heading: [%s]\n", no);
    CHECK(!TextBook::is_heading(no));
  }
}

std::vector<std::string> split(const std::string& text, bool wrapped) {
  std::string buf = text + '\0';
  const char* paragraphs[64];
  const uint32_t n =
      TextBook::split_text(buf.data(), static_cast<uint32_t>(text.size()), buf.data(), paragraphs, 64, wrapped, 0);
  return std::vector<std::string>(paragraphs, paragraphs + n);
}

void test_text_paragraphs() {
  // Wrapped text: lines join up to a blank or indented line.
  const std::string wrapped =
      "It was a bright cold day in April, and the\r\nclocks were striking thirteen.\r\n\r\n"
      "Winston Smith,  his chin\r\nnuzzled into his breast.\r\n  An indented line\r\nstarts one too.\r\n";
  std::vector<std::string> want = {"It was a bright cold day in April, and the clocks were striking thirteen.",
                                   "Winston Smith, his chin nuzzled into his breast.",
                                   "An indented line starts one too."};
  CHECK(split(wrapped, true) == want);
  // Otherwise every line is a paragraph.
  want = {"It was a bright cold day in April, and the",
          "clocks were striking thirteen.",
          "Winston Smith, his chin",
          "nuzzled into his breast.",
          "An indented line",
          "starts one too."};
  CHECK(split(wrapped, false) == want);
  // A heading and the short title after it are headings; the text is not.
  const std::string chapters = "CHAPTER I.\nDown the Rabbit-Hole\n\nAlice was beginning\nto get very tired.\n\nXII\n";
  std::string buf = chapters + std::string(8, '\0');
  const char* paragraphs[16];
  // Converted with room in front, as TextBook does: marks add a byte each.
  std::memmove(buf.data() + 4, buf.data(), chapters.size());
  const uint32_t n =
      TextBook::split_text(buf.data() + 4, static_cast<uint32_t>(chapters.size()), buf.data(), paragraphs, 16, true, 4);
  want = {
      "\x01"
      "CHAPTER I.",
      "\x01"
      "Down the Rabbit-Hole",
      "Alice was beginning to get very tired.",
      "\x01"
      "XII"};
  CHECK(std::vector<std::string>(paragraphs, paragraphs + n) == want);
}

void test_text_book_1257() {
  StdioStorage data(VELLUM_TEST_DATA);
  TextBook book;
  CHECK(book.open(data, "sample-1257.txt"));
  CHECK(book.format() == TextBook::Format::Text);
  CHECK(book.encoding() == charset::Encoding::Windows1257);
  CHECK(std::string(book.info().title) == "sample-1257");
  CHECK(std::string(book.info().language) == "lt");
  CHECK(book.chapter_count() == 3);
  CHECK(book.toc_count() == 3);
  if (book.toc_count() == 3) {
    CHECK(std::string(book.toc(0).title) == "Vellum bandymas");
    CHECK(std::string(book.toc(1).title) == "1 skyrius \xC2\xB7 Kelionė");
    CHECK(std::string(book.toc(2).title) == "2 skyrius");
    CHECK(book.toc(1).chapter == 1 && book.toc(2).chapter == 2 && book.toc(2).paragraph == 0);
  }
  Chapter ch{};
  CHECK(book.load_chapter(1, ch));
  const std::vector<std::string> want = {
      "\x01"
      "1 skyrius",
      "\x01"
      "Kelionė",
      "Tai buvo graži diena, ir žmonės ėjo į turgų pirkti šviežios duonos.",
      "Antra pastraipa prasideda įtrauka ir tęsiasi čia."};
  CHECK(std::vector<std::string>(ch.paragraphs, ch.paragraphs + ch.paragraph_count) == want);
  CHECK(book.load_chapter(2, ch) && ch.paragraph_count == 2);
  CHECK(!book.load_chapter(3, ch));
  // Weights follow the parts' lengths.
  CHECK(book.chapter_weight(0) < book.chapter_weight(1));
}

void test_text_book_long_and_named() {
  // No headings, a name with the author, UTF-8: parts named by their first
  // words, so contents and percent still work.
  StdioStorage card("test-card-text");
  std::string text;
  for (int i = 0; text.size() < 3 * TextBook::kSplitBytes; ++i) {
    text += "Paragraph " + std::to_string(i) + " is here, with words enough to read as a real line of text.\n";
  }
  CHECK(card.write("books/Jane Doe - Long_Book.txt", text.data(), static_cast<uint32_t>(text.size())));
  TextBook book;
  CHECK(book.open(card, "books/Jane Doe - Long_Book.txt"));
  CHECK(std::string(book.info().title) == "Long Book");
  CHECK(std::string(book.info().author) == "Jane Doe");
  CHECK(book.chapter_count() >= 3);
  CHECK(book.toc_count() == book.chapter_count());
  CHECK(std::string(book.toc(0).title).starts_with("Paragraph 0 is here"));
  CHECK(std::string(book.toc(1).title).ends_with("\xE2\x80\xA6"));
  // Every paragraph is in exactly one part.
  uint32_t total = 0;
  for (int i = 0; i < book.chapter_count(); ++i) {
    Chapter ch{};
    CHECK(book.load_chapter(i, ch));
    total += ch.paragraph_count;
    if (i > 0) CHECK(std::string(ch.paragraphs[0]).starts_with("Paragraph "));
  }
  CHECK(total == static_cast<uint32_t>(std::count(text.begin(), text.end(), '\n')));

  // UTF-16 with a byte order mark, and Mac line breaks.
  std::string u16 = "\xFF\xFE";
  for (char c : std::string("Chapter 1\rHello there.\rChapter 2\rGoodbye.\r")) u16 += std::string{c, '\0'};
  CHECK(card.write("books/wide.txt", u16.data(), static_cast<uint32_t>(u16.size())));
  CHECK(book.open(card, "books/wide.txt"));
  CHECK(book.encoding() == charset::Encoding::Utf16LE);
  CHECK(book.chapter_count() == 2 && book.toc_count() == 2);
  Chapter ch{};
  CHECK(book.load_chapter(1, ch) && ch.paragraph_count == 2);
  CHECK(std::string(ch.paragraphs[0]) ==
            "\x01"
            "Chapter 2" &&
        std::string(ch.paragraphs[1]) == "Goodbye.");
}

void check_sample_fb2(TextBook& book) {
  CHECK(std::string(book.info().title) == "Vellum & FB2");
  CHECK(std::string(book.info().author) == "Arūnas T. Testeris");
  CHECK(std::string(book.info().language) == "lt");
  // The annotation, the body's title, three sections and the notes.
  CHECK(book.chapter_count() == 6);
  CHECK(book.toc_count() == 5);
  if (book.chapter_count() != 6 || book.toc_count() != 5) return;
  CHECK(std::string(book.toc(0).title) == "Vellum & FB2" && book.toc(0).chapter == 0);
  CHECK(std::string(book.toc(1).title) == "Pirmas skyrius \xC2\xB7 Kelionė");
  CHECK(book.toc(1).chapter == 2 && book.toc(1).depth == 0 && book.toc(1).paragraph == 0);
  CHECK(std::string(book.toc(2).title) == "Eilėraštis" && book.toc(2).depth == 1 && book.toc(2).chapter == 2);
  CHECK(std::string(book.toc(3).title) == "Antras skyrius" && book.toc(3).chapter == 4);
  CHECK(std::string(book.toc(4).title) == "Pastabos" && book.toc(4).chapter == 5);

  Chapter ch{};
  CHECK(book.load_chapter(0, ch));
  std::vector<std::string> want = {
      "\x01"
      "Vellum & FB2",
      "\x01"
      "Arūnas T. Testeris",
      "Trumpas aprašymas.", "Antra pastraipa."};
  CHECK(std::vector<std::string>(ch.paragraphs, ch.paragraphs + ch.paragraph_count) == want);
  CHECK(book.toc(2).paragraph == TocEntry::kUnknown);
  CHECK(book.load_chapter(2, ch));
  want = {
      "\x01"
      "Pirmas skyrius",
      "\x01"
      "Kelionė",
      "Labas, pasauli![1]",
      "",
      "\x01"
      "Eilėraštis",
      "pirma eilė",
      "antra eilė"};
  const std::vector<std::string> got(ch.paragraphs, ch.paragraphs + ch.paragraph_count);
  CHECK(got == want);
  CHECK(book.toc(2).paragraph == 4);  // the nested section's place, found by reading its chapter
  CHECK(book.load_chapter(4, ch) && ch.paragraph_count == 3 && std::string(ch.paragraphs[1]) == "\x01* * *");
  CHECK(book.load_chapter(5, ch) && ch.paragraph_count == 3 && std::string(ch.paragraphs[2]) == "Pastaba.");

  const uint8_t* picture = nullptr;
  const int32_t len = book.read_cover(&picture);
  CHECK(len > 8 && std::memcmp(picture, "\x89PNG", 4) == 0);
  CHECK(book.load_chapter(2, ch) && ch.paragraph_count == 7);  // read again after the cover used the buffer
}

void test_fb2_book() {
  StdioStorage data(VELLUM_TEST_DATA);
  TextBook book;
  CHECK(book.open(data, "sample.fb2"));
  CHECK(book.format() == TextBook::Format::Fb2);
  check_sample_fb2(book);

  std::string text =
      "<section><title><p>A</p></title><p>One <emphasis>two</emphasis></p>"
      "<section><p>x</p></section><cite><p>Quote</p><text-author>Me</text-author></cite></section>";
  const char* paragraphs[16];
  std::vector<uint32_t> sections;
  const uint32_t n = TextBook::fb2_paragraphs(
      text.data(), static_cast<uint32_t>(text.size()), paragraphs, 16,
      [](void* ctx, uint32_t s, uint32_t p) { static_cast<std::vector<uint32_t>*>(ctx)->push_back(s * 100 + p); },
      &sections);
  const std::vector<std::string> want = {
      "\x01"
      "A",
      "One two", "x", "Quote", "Me"};
  CHECK(std::vector<std::string>(paragraphs, paragraphs + n) == want);
  CHECK((sections == std::vector<uint32_t>{100, 202}));
}

void test_fb2_zip_book() {
  StdioStorage card("test-card-fb2");
  const std::string zipped = read_all(VELLUM_TEST_DATA "/sample.fb2.zip");
  CHECK(card.write("books/sample.fb2.zip", zipped.data(), static_cast<uint32_t>(zipped.size())));
  for (int pass = 0; pass < 2; ++pass) {  // unpacked once, then the copy is used
    TextBook book;
    CHECK(book.open(card, "books/sample.fb2.zip"));
    CHECK(book.format() == TextBook::Format::Fb2Zip);
    check_sample_fb2(book);
  }
  const std::string unpacked = read_all(VELLUM_TEST_DATA "/sample.fb2");
  bool found = false;
  for (const auto& f : std::filesystem::directory_iterator("test-card-fb2/vellum/unzipped")) {
    found |= f.path().extension() == ".fb2" && read_all(f.path().c_str()) == unpacked;
  }
  CHECK(found);
}

void test_html_book() {
  StdioStorage card("test-card-html");
  const std::string page =
      "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"windows-1252\"><title>A Caf\xE9 Page</title>"
      "<meta name=\"author\" content=\"Ann Writer\"><style>p{}</style></head><body>"
      "<h1>Part One</h1><h2>Morning</h2><p>Coffee at the caf\xE9.</p><h2>Evening</h2><p>Tea.</p></body></html>";
  CHECK(card.write("books/page.html", page.data(), static_cast<uint32_t>(page.size())));
  TextBook book;
  CHECK(book.open(card, "books/page.html"));
  CHECK(std::string(book.info().title) == "A Café Page");
  CHECK(std::string(book.info().author) == "Ann Writer");
  CHECK(std::string(book.info().language) == "en");
  CHECK(book.chapter_count() == 3);
  CHECK(book.toc_count() == 3);
  if (book.toc_count() == 3) {
    CHECK(std::string(book.toc(0).title) == "Part One" && book.toc(0).depth == 0);
    CHECK(std::string(book.toc(1).title) == "Morning" && book.toc(1).depth == 1 && book.toc(1).chapter == 1);
  }
  Chapter ch{};
  CHECK(book.load_chapter(1, ch) && ch.paragraph_count == 2);
  CHECK(std::string(ch.paragraphs[0]) == "\x01Morning" && std::string(ch.paragraphs[1]) == "Coffee at the café.");
}

void test_library_finds_other_formats() {
  StdioStorage card("test-card-formats");
  std::remove("test-card-formats/vellum/library.txt");
  for (const char* name : {"sample.fb2", "sample.fb2.zip", "sample-1257.txt"}) {
    const std::string data = read_all((std::string(VELLUM_TEST_DATA "/") + name).c_str());
    CHECK(card.write((std::string("books/") + name).c_str(), data.data(), static_cast<uint32_t>(data.size())));
  }
  CHECK(card.write("books/notes.pdf", "%PDF", 4));
  Epub epub;
  TextBook text;
  Library lib;
  lib.load(builtin_books(), &card, epub, &text);
  CHECK(lib.count() == 4 + 3);
  int fb2 = 0, txt = 0;
  for (int i = 0; i < lib.count(); ++i) {
    const LibraryEntry& e = lib.entry(i);
    if (std::string(e.title) == "Vellum & FB2") {
      ++fb2;
      CHECK(e.cover == 1);
      CHECK(std::string(e.author) == "Arūnas T. Testeris");
    }
    if (std::string(e.path) == "books/sample-1257.txt") {
      ++txt;
      CHECK(e.cover == 0 && std::string(e.language) == "lt");
    }
  }
  CHECK(fb2 == 2 && txt == 1);
  // Without a TextBook they are left out, as before.
  Library epubs_only;
  epubs_only.load(builtin_books(), &card, epub);
  CHECK(epubs_only.count() >= 4);
  CHECK(is_book_file("a.FB2.ZIP") && is_book_file("b.htm") && is_book_file("c.Txt") && !is_book_file("d.zip"));
}

void test_book_upload_names() {
  CHECK(safe("Knyga.txt") == "Knyga.txt");
  CHECK(safe("Knyga.FB2") == "Knyga.FB2");
  CHECK(safe("Knyga.fb2.zip") == "Knyga.fb2.zip");
  CHECK(safe("page.html") == "page.html");
  CHECK(safe("page.htm") == "page.htm");
  for (const char* no : {"x.zip", "x.pdf", "x.mobi", "x.azw3", "x.docx", ".txt"}) CHECK(safe(no) == "<rejected>");
  // A long name is cut, keeping the whole of a two-part extension.
  const std::string longer = std::string(200, 'a') + ".fb2.zip";
  const std::string cut = safe(longer.c_str());
  CHECK(cut.size() == 120 && cut.ends_with("a.fb2.zip"));
}

}  // namespace

// Mean grey of a band of rows in a thumbnail.
int band_mean(const CoverImage& img, int y0, int y1) {
  long sum = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < img.w; ++x) sum += img.px[y * img.w + x];
  return static_cast<int>(sum / ((y1 - y0) * img.w));
}

std::string font_family(const char* file, bool* italic_out = nullptr) {
  char family[48];
  bool italic = false;
  if (!parse_font_file_name(file, family, sizeof(family), italic)) return "<rejected>";
  if (italic_out != nullptr) *italic_out = italic;
  return family;
}

void test_font_files() {
  bool italic = true;
  CHECK(font_family("Merriweather-Regular.ttf", &italic) == "Merriweather" && !italic);
  CHECK(font_family("Merriweather-Italic.ttf", &italic) == "Merriweather" && italic);
  CHECK(font_family("PlayfairDisplay-Regular.otf") == "Playfair Display");
  CHECK(font_family("EBGaramond-VariableFont_wght.ttf") == "EB Garamond");
  CHECK(font_family("EBGaramond-Italic-VariableFont_wght.ttf", &italic) == "EB Garamond" && italic);
  CHECK(font_family("Literata_18pt-Regular.ttf") == "Literata");
  CHECK(font_family("Literata[opsz,wght].ttf") == "Literata");
  CHECK(font_family("Gentium.TTF") == "Gentium");
  CHECK(font_family("Merriweather-Bold.ttf") == "<rejected>");  // other weights are not used
  CHECK(font_family("notes.txt") == "<rejected>");
  CHECK(font_family(".ttf") == "<rejected>");

  char out[160];
  CHECK(safe_font_name("C:\\fakepath\\Lora-Regular.ttf", out, sizeof(out)) && std::string(out) == "Lora-Regular.ttf");
  CHECK(safe_font_name("x.OTF", out, sizeof(out)));
  CHECK(!safe_font_name("book.epub", out, sizeof(out)));
  CHECK(!safe_book_name("Lora-Regular.ttf", out, sizeof(out)));
}

void test_raster_font() {
  TtfFace face;
  CHECK(!face.load(reinterpret_cast<const uint8_t*>("not a font at all"), 17));
  CHECK(face.load(fonts::kAtkinsonRegular, fonts::kAtkinsonRegularSize));
  RasterFont f;
  CHECK(f.build(face, 26, "Atkinson Hyperlegible Next"));
  const Font& font = f.font();
  CHECK(font.size == 26 && font.glyph_count > 250 && font.line_height >= 26 && font.ascent > font.descent);
  for (uint32_t cp : {0x0105u, 0x010Du, 0x0119u, 0x0117u, 0x012Fu, 0x0161u, 0x0173u, 0x016Bu, 0x017Eu}) {
    CHECK(font.find(cp) != font.fallback);  // ą č ę ė į š ų ū ž
  }
  CHECK(font.glyphs[font.find('A')].codepoint == 'A');
  CHECK(font.glyphs[font.find(0x0105)].codepoint == 0x0105);  // ą
  CHECK(font.find(0x4E2D) == font.fallback);                  // a character it does not have
  for (uint16_t i = 1; i < font.glyph_count; ++i) CHECK(font.glyphs[i - 1].codepoint < font.glyphs[i].codepoint);
  const int w = text_width_px(font, "Vellum");
  CHECK(w > 50 && w < 120);
  // Wider at a larger size; kerning tables stay sorted.
  RasterFont big;
  CHECK(big.build(face, 40, "Atkinson Hyperlegible Next"));
  CHECK(text_width_px(big.font(), "Vellum") > w);
  for (uint16_t i = 1; i < big.font().kerning_count; ++i) {
    const KernPair& a = big.font().kerning[i - 1];
    const KernPair& b = big.font().kerning[i];
    CHECK(a.left < b.left || (a.left == b.left && a.right < b.right));
  }
  // Drawn, it leaves ink.
  Fixture fx;
  fx.fb.clear(Color::White);
  draw_text(fx.fb, font, 10 * 64, 40, "Hello");
  int ink = 0;
  for (int y = 0; y < 60; ++y) {
    for (int x = 0; x < 120; ++x) ink += fx.fb.pixel(x, y) == Color::Black ? 1 : 0;
  }
  CHECK(ink > 50);
}

void test_font_library() {
  std::remove("test-card/fonts/MyFace-Regular.ttf");
  std::remove("test-card/fonts/MyFace-Bold.ttf");
  std::remove("test-card/fonts/Broken-Regular.ttf");
  StdioStorage card("test-card");
  CHECK(card.write("fonts/MyFace-Regular.ttf", fonts::kEbgaramondRegular, fonts::kEbgaramondRegularSize));
  CHECK(card.write("fonts/MyFace-Bold.ttf", fonts::kEbgaramondRegular, fonts::kEbgaramondRegularSize));
  CHECK(card.write("fonts/Broken-Regular.ttf", "nonsense", 8));
  FontLibrary lib;
  CHECK(lib.count() == 3 && std::string(lib.name(0)) == "Literata");
  lib.scan(&card);
  CHECK(lib.count() == 5);
  const int mine = lib.find("My Face");
  const int broken = lib.find("Broken");
  CHECK(mine >= 3 && broken >= 3 && lib.on_card(mine) && !lib.on_card(1));
  MenuFonts menu = literata_menu();
  CHECK(!lib.menu(broken, menu) && menu.label == &fonts::literata_16);  // unchanged
  CHECK(lib.menu(mine, menu) && menu.label->size == 16 && menu.title->size == 40);
  CHECK(std::string(menu.label->family) == "My Face");
  BookFonts book = literata_book(26);
  CHECK(lib.book(1, 30, book) && book.body->size == 30 && book.heading->size == 26);
  CHECK(lib.book(0, 34, book) && book.body == &fonts::literata_34);
  std::remove("test-card/fonts/MyFace-Regular.ttf");
  std::remove("test-card/fonts/MyFace-Bold.ttf");
  std::remove("test-card/fonts/Broken-Regular.ttf");
}

void test_app_fonts() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/state.txt");
  StdioStorage card("test-card");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  CHECK(ui::menu_fonts().label == &fonts::literata_16);
  press(*app, Key::Prev);  // Settings, the last link
  press(*app, Key::Power);
  press(*app, Key::Next);  // Menu font
  press(*app, Key::Power);
  CHECK(read_all("test-card/vellum/settings.txt").find("menu_font=Atkinson Hyperlegible Next") != std::string::npos);
  CHECK(std::string(ui::menu_fonts().label->family) == "Atkinson Hyperlegible Next");
  for (int i = 0; i < 11; ++i) press(*app, Key::Next);  // down to Reading
  press(*app, Key::Power);
  press(*app, Key::Power);  // Book font: the next after Literata
  CHECK(read_all("test-card/vellum/settings.txt").find("book_font=Atkinson Hyperlegible Next") != std::string::npos);
  press(*app, Key::Power);
  CHECK(read_all("test-card/vellum/settings.txt").find("book_font=EB Garamond") != std::string::npos);
  delete app;

  // Remembered on the next start; a font that is gone falls back to Literata.
  const char* saved = "menu_font=Gone Font\nbook_font=EB Garamond\n";
  card.write("vellum/settings.txt", saved, static_cast<uint32_t>(std::strlen(saved)));
  app = new App(board);
  app->start();
  CHECK(ui::menu_fonts().label == &fonts::literata_16);
  delete app;
  std::remove("test-card/vellum/settings.txt");
  ui::set_menu_fonts(literata_menu());
}

void test_update_helpers() {
  CHECK(is_newer_version("0.1.43", "0.1.42"));
  CHECK(is_newer_version("v0.2.0", "0.1.99"));
  CHECK(is_newer_version("0.1.10", "0.1.9"));
  CHECK(!is_newer_version("0.1.42", "0.1.42"));
  CHECK(!is_newer_version("0.1.41", "0.1.42"));
  CHECK(is_newer_version("0.1.1", "dev"));  // builds of one's own are older than any release
  CHECK(is_newer_version("0.1.1", "890524d-dirty"));
  CHECK(!is_newer_version("", "0.1.1"));
  CHECK(!is_newer_version("nightly", "0.1.1"));
  CHECK(!is_newer_version("0.1.x", "dev"));

  const char* json =
      "{\"url\":\"https://api.github.com/x\",\"tag_name\": \"v0.1.50\",\"name\":\"Vellum 0.1.50\","
      "\"assets\":[{\"name\":\"vellum-0.1.50-usb.zip\",\"browser_download_url\":"
      "\"https://github.com/ArunasVell/Vellum-OS/releases/download/v0.1.50/vellum-0.1.50-usb.zip\"},"
      "{\"name\":\"vellum-0.1.50.bin\",\"browser_download_url\":"
      "\"https://github.com/ArunasVell/Vellum-OS/releases/download/v0.1.50/vellum-0.1.50.bin\"}]}";
  char version[32];
  char url[256];
  CHECK(parse_release(json, std::strlen(json), version, sizeof(version), url, sizeof(url)));
  CHECK(std::string(version) == "0.1.50");
  CHECK(std::string(url) == "https://github.com/ArunasVell/Vellum-OS/releases/download/v0.1.50/vellum-0.1.50.bin");
  const char* no_bin = "{\"tag_name\":\"v0.1.50\",\"assets\":[]}";
  CHECK(!parse_release(no_bin, std::strlen(no_bin), version, sizeof(version), url, sizeof(url)));
  const char* not_found = "{\"message\":\"Not Found\"}";
  CHECK(!parse_release(not_found, std::strlen(not_found), version, sizeof(version), url, sizeof(url)));

  CHECK(seconds_until_hour({2026, 10, 4, 2, 59, 30}, 3) == 30);
  CHECK(seconds_until_hour({2026, 10, 4, 3, 0, 0}, 3) == 86400);
  CHECK(seconds_until_hour({2026, 10, 4, 22, 0, 0}, 3) == 5 * 3600);

  char out[40];
  const char* form = "ssid=My+Home%20Net&password=p%26ss&x";
  CHECK(form_field(form, std::strlen(form), "ssid", out, sizeof(out)) && std::string(out) == "My Home Net");
  CHECK(form_field(form, std::strlen(form), "password", out, sizeof(out)) && std::string(out) == "p&ss");
  CHECK(form_field(form, std::strlen(form), "x", out, sizeof(out)) && std::string(out).empty());
  CHECK(!form_field(form, std::strlen(form), "pass", out, sizeof(out)));

  std::remove("test-card/vellum/update.txt");
  StdioStorage card("test-card");
  UpdateState st;
  load_update_state(card, st);
  CHECK(st.latest[0] == '\0' && !st.has_checked && !st.night_restart);
  std::snprintf(st.latest, sizeof(st.latest), "0.1.50");
  st.checked = {2026, 10, 5, 3, 0, 12};
  st.has_checked = true;
  st.night_restart = true;
  CHECK(save_update_state(card, st));
  UpdateState back;
  load_update_state(card, back);
  CHECK(std::string(back.latest) == "0.1.50" && back.has_checked && back.checked.day == 5 &&
        back.checked.second == 12 && back.night_restart);
  std::remove("test-card/vellum/update.txt");
}

class FakeUpdater : public Updater {
 public:
  void home_network(char* out, size_t size) override { std::snprintf(out, size, "%s", network); }
  UpdateResult check(const char* current, char* latest, size_t size) override {
    ++checks;
    std::snprintf(latest, size, "%s", newest);
    if (check_result != UpdateResult::Available) return check_result;
    return is_newer_version(newest, current) ? UpdateResult::Available : UpdateResult::UpToDate;
  }
  UpdateResult install(ProgressFn progress, void* ctx) override {
    ++installs;
    for (int p = 0; p <= 100 && progress != nullptr; p += 25) progress(ctx, p);
    return install_result;
  }
  const char* network = "Home";
  const char* newest = "0.1.50";
  UpdateResult check_result = UpdateResult::Available;
  UpdateResult install_result = UpdateResult::Installed;
  int checks = 0;
  int installs = 0;
};

class NightPower : public FixedPower {
 public:
  void sleep() override { ++sleeps; }
  void restart() override { ++restarts; }
  void set_wake_timer(uint32_t seconds) override { wake_after = seconds; }
  bool woke_by_timer() override { return timer; }
  int sleeps = 0;
  int restarts = 0;
  uint32_t wake_after = 0;
  bool timer = false;
};

void test_app_updates_screen() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  NightPower power;
  TestClock clock;
  std::remove("test-card/vellum/state.txt");
  std::remove("test-card/vellum/update.txt");
  std::remove("test-card/vellum/settings.txt");
  StdioStorage card("test-card");
  FakeTransfer transfer;
  FakeUpdater updater;
  updater.check_result = UpdateResult::NoWifi;
  Board board{"test", panel, input, power, nullptr, &clock, &card, Rotation::R90, buf.data(), &transfer};
  board.version = "0.1.42";
  board.updater = &updater;
  auto* app = new App(board);
  app->start();

  tap(*app, 240, 700);  // Settings
  tap(*app, 240, 394);  // Updates, the eighth row
  tap(*app, 240, 325);  // Check now: the home Wi-Fi is out of reach
  CHECK(updater.checks == 1 && updater.installs == 0);
  CHECK(read_all("test-card/vellum/update.txt").empty());  // a failed check remembers nothing

  updater.check_result = UpdateResult::Available;
  tap(*app, 240, 325);  // again: 0.1.50 is out
  CHECK(updater.checks == 2 && updater.installs == 0);
  CHECK(read_all("test-card/vellum/update.txt").find("latest=0.1.50") != std::string::npos);

  tap(*app, 240, 375);  // Automatic: at night -> ask first
  CHECK(read_all("test-card/vellum/settings.txt").find("updates=1") != std::string::npos);

  press(*app, Key::Home);  // home shows the notice; tapping it opens Updates
  tap(*app, 420, 90);
  tap(*app, 240, 325);  // Install Vellum 0.1.50
  CHECK(updater.installs == 1 && power.restarts == 1);
  CHECK(read_all("test-card/vellum/state.txt") == "home\n");
  CHECK(read_all("test-card/vellum/update.txt").find("latest=\n") != std::string::npos);

  // Sleeping sets the nightly wake: 14:05 now, 3:00 is 12 h 55 min away.
  press(*app, Key::Home);
  InputEvent hold = key_event(InputEvent::Type::KeyDown, Key::Power);
  app->handle(hold);
  app->timeout();  // held a second: sleep
  CHECK(power.sleeps == 1);
  DateTime now;
  clock.now(now);
  CHECK(power.wake_after == seconds_until_hour(now, kNightHour));
  delete app;

  // No home Wi-Fi: no wake timer.
  updater.network = "";
  power.wake_after = 7;
  app = new App(board);
  app->start();
  app->handle(hold);
  app->timeout();
  CHECK(power.wake_after == 0);
  delete app;
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/update.txt");
}

void test_app_night_update() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  NightPower power;
  TestClock clock;
  std::remove("test-card/vellum/update.txt");
  std::remove("test-card/vellum/settings.txt");
  StdioStorage card("test-card");
  card.write("vellum/state.txt", "reader\n", 7);
  FakeUpdater updater;
  Board board{"test", panel, input, power, nullptr, &clock, &card, Rotation::R90, buf.data()};
  board.version = "0.1.42";
  board.updater = &updater;

  // The timer wakes it at night: a newer version is installed without
  // touching the screen, and the reader restarts into it.
  power.timer = true;
  auto* app = new App(board);
  app->start();
  CHECK(updater.checks == 1 && updater.installs == 1 && power.restarts == 1);
  CHECK(panel.full == 0 && panel.fast == 0);
  CHECK(read_all("test-card/vellum/update.txt").find("night_restart=1") != std::string::npos);
  CHECK(read_all("test-card/vellum/state.txt") == "reader\n");  // a button still wakes it into the book
  delete app;

  // The new version's first start: straight back to sleep, nothing drawn.
  power.timer = false;
  board.version = "0.1.50";
  app = new App(board);
  app->start();
  CHECK(power.sleeps == 1 && updater.checks == 1);
  CHECK(panel.full == 0 && panel.fast == 0);
  CHECK(read_all("test-card/vellum/update.txt").find("night_restart=0") != std::string::npos);
  delete app;

  // Ask first: the night only looks, and home shows the notice.
  card.write("vellum/settings.txt", "updates=1\n", 10);
  updater.newest = "0.1.51";
  power.timer = true;
  app = new App(board);
  app->start();
  CHECK(updater.checks == 2 && updater.installs == 1 && power.sleeps == 2);
  CHECK(read_all("test-card/vellum/update.txt").find("latest=0.1.51") != std::string::npos);
  delete app;

  // Off: nothing is looked for.
  card.write("vellum/settings.txt", "updates=2\n", 10);
  app = new App(board);
  app->start();
  CHECK(updater.checks == 2 && power.sleeps == 3 && power.wake_after == 0);
  delete app;
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/update.txt");
  std::remove("test-card/vellum/state.txt");
}

void test_covers() {
  // Both sample books carry a test card: white top, grey band, black bottom.
  // sample.epub has a JPEG (EPUB 3 cover-image), sample-ncx.epub a PNG
  // (EPUB 2 meta name=cover).
  StdioStorage data(VELLUM_TEST_DATA);
  StdioStorage card("test-card");
  std::vector<uint8_t> large(covers::kLargeW * covers::kLargeH);
  std::vector<uint8_t> small(covers::kSmallW * covers::kSmallH);
  for (const char* name : {"sample.epub", "sample-ncx.epub"}) {
    Epub epub;
    CHECK(epub.open(data, name));
    const uint8_t* picture = nullptr;
    const int32_t len = epub.read_cover(&picture);
    CHECK(len > 0 && picture != nullptr);
    if (len <= 0) continue;
    char path[64];
    covers::path_for(name, 1234, path, sizeof(path));
    CHECK(std::string(path).starts_with("vellum/covers/"));
    CHECK(covers::make(card, path, picture, static_cast<uint32_t>(len)));
    for (bool big : {true, false}) {
      CoverImage img;
      img.px = big ? large.data() : small.data();
      CHECK(covers::load(card, path, big, img));
      CHECK(img.w > 0 && img.h > 0);
      CHECK(img.w <= (big ? covers::kLargeW : covers::kSmallW));
      CHECK(img.h <= (big ? covers::kLargeH : covers::kSmallH));
      if (!img.valid()) continue;
      CHECK(band_mean(img, 0, img.h / 5) > 220);
      CHECK(band_mean(img, img.h * 9 / 10, img.h) < 40);
    }
  }
  // Broken data is refused and leaves no thumbnails behind.
  const uint8_t junk[] = {0xFF, 0xD8, 0xFF, 0x00, 1, 2, 3};
  CHECK(!covers::make(card, "vellum/covers/junk.cov", junk, sizeof(junk)));
  CoverImage none;
  none.px = small.data();
  CHECK(!covers::load(card, "vellum/covers/junk.cov", false, none));
  // Different books get different files.
  char a[64], b[64];
  covers::path_for("books/a.epub", 10, a, sizeof(a));
  covers::path_for("books/a.epub", 11, b, sizeof(b));
  CHECK(std::string(a) != b);

  // The library makes thumbnails once and remembers that in its cache.
  std::remove("test-card/vellum/library.txt");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample-ncx.epub");
  CHECK(card.write("books/ncx.epub", book.data(), static_cast<uint32_t>(book.size())));
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  int found = -1;
  for (int i = 0; i < lib.count(); ++i) {
    if (std::string(lib.entry(i).path) == "books/ncx.epub") found = i;
    if (lib.entry(i).builtin) CHECK(lib.entry(i).cover <= 0);
  }
  CHECK(found >= 0);
  if (found < 0) return;
  CHECK(lib.entry(found).cover == 1);
  Library again;
  again.load(builtin_books(), &card, epub);
  for (int i = 0; i < again.count(); ++i) {
    if (std::string(again.entry(i).path) == "books/ncx.epub") CHECK(again.entry(i).cover == 1);
  }
  // A cache from before the full-screen sleep cover ("1") makes the cover again, larger.
  std::string cache = read_all("test-card/vellum/library.txt");
  const size_t at = cache.find("books/ncx.epub\t");
  CHECK(at != std::string::npos);
  // The cover mark comes before the last field, the order the book was added in.
  const size_t end = cache.find('\n', at);
  const size_t mark = end == std::string::npos ? end : cache.rfind('\t', end) - 2;
  CHECK(mark != std::string::npos && cache.compare(mark, 3, "\t2\t") == 0);
  if (mark == std::string::npos) return;
  cache[mark + 1] = '1';
  CHECK(card.write("vellum/library.txt", cache.data(), static_cast<uint32_t>(cache.size())));
  char ncx_cover[64];
  covers::path_for("books/ncx.epub", static_cast<uint32_t>(book.size()), ncx_cover, sizeof(ncx_cover));
  std::remove((std::string("test-card/") + ncx_cover).c_str());
  Library upgraded;
  upgraded.load(builtin_books(), &card, epub);
  CHECK(std::filesystem::exists(std::string("test-card/") + ncx_cover));
  CHECK(read_all("test-card/vellum/library.txt").find("\t2\t", at) == mark);
  std::remove("test-card/books/ncx.epub");

  // A picture drawn into a cover: light at the top, dark at the bottom,
  // dithered grey in between.
  CoverImage img;
  img.px = large.data();
  char path[64];
  covers::path_for("sample.epub", 1234, path, sizeof(path));
  CHECK(covers::load(card, path, true, img));
  Fixture f;
  f.fb.clear(Color::White);
  const Rect r{100, 40, 120, 180};
  ui::draw_picture(f.fb, r, img);
  auto blacks = [&](int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
      for (int x = r.x; x < r.x + r.w; ++x) n += f.fb.pixel(x, y) == Color::Black ? 1 : 0;
    return n * 100 / ((y1 - y0) * r.w);
  };
  CHECK(blacks(r.y + 2, r.y + 30) < 5);
  CHECK(blacks(r.y + r.h - 12, r.y + r.h) > 95);
  const int mid = blacks(r.y + r.h / 2 - 5, r.y + r.h / 2 + 5);
  CHECK(mid > 10 && mid < 90);
  CHECK(f.fb.pixel(r.x - 1, r.y + r.h - 2) == Color::White);  // nothing drawn outside
}

void test_app_device_check() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  Board board{"test", panel, input, power, nullptr, nullptr, nullptr, Rotation::R90, buf.data()};
  Framebuffer view(buf.data(), kW, kH);
  view.set_rotation(Rotation::R90);
  // The touch box's left edge runs down the side margin (28); on Settings a
  // highlighted row starts further left.
  auto on_check = [&] { return view.pixel(28, 340) == Color::Black && view.pixel(26, 340) == Color::White; };
  auto* app = new App(board);
  app->start();
  for (int i = 0; i < 5; ++i) press(*app, Key::Next);  // the current book, 2 more, 3 links: Settings
  press(*app, Key::Power);
  for (int i = 0; i < 9; ++i)
    press(*app, Key::Next);  // Language, Menu font, Clock, Sleep after, Sleep screen, Light, Battery, Updates, Stats
  CHECK(!on_check());
  press(*app, Key::Power);
  CHECK(on_check());

  // Buttons and touch only show themselves; so does the first Home press.
  press(*app, Key::Next);
  press(*app, Key::Prev);
  press(*app, Key::Home);
  CHECK(on_check());
  CHECK(view.pixel(240 + 12, 700) == Color::White);
  tap(*app, 240, 700);
  CHECK(view.pixel(240 + 12, 700) == Color::Black);  // crosshair where the finger was
  CHECK(view.pixel(240, 700 - 12) == Color::Black);
  const int full = panel.full;
  press(*app, Key::Power);  // cleans the screen with a full refresh
  CHECK(panel.full == full + 1 && on_check());

  press(*app, Key::Home);  // Home a second time: back to Settings
  CHECK(!on_check());
  press(*app, Key::Power);  // reopened fresh: one Home press stays
  CHECK(on_check());
  press(*app, Key::Home);
  CHECK(on_check());
  press(*app, Key::Home);
  CHECK(!on_check());
  delete app;
}

void test_app_device_check_memory() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  Board board{"test", panel, input, power, nullptr, nullptr, nullptr, Rotation::R90, buf.data()};
  Framebuffer view(buf.data(), kW, kH);
  view.set_rotation(Rotation::R90);
  // Ink in the row under Wi-Fi, where free memory goes.
  auto memory_row_inked = [&] {
    for (int y = 708; y < 732; ++y)
      for (int x = 28; x < kH - 28; ++x)
        if (view.pixel(x, y) == Color::Black) return true;
    return false;
  };
  auto open_check = [&](App& app) {
    app.start();
    for (int i = 0; i < 5; ++i) press(app, Key::Next);  // Settings
    press(app, Key::Power);
    for (int i = 0; i < 9; ++i) press(app, Key::Next);  // Device check
    press(app, Key::Power);
  };
  auto* app = new App(board);
  open_check(*app);
  CHECK(!memory_row_inked());  // the board does not tell: no row
  delete app;

  static int asked = 0;
  board.memory = [](MemoryInfo& m) {
    ++asked;
    m.internal_free = 150 * 1024;
    m.external_free = 7 * 1024 * 1024 + 300 * 1024;  // 7.3 MB
  };
  app = new App(board);
  open_check(*app);
  CHECK(asked > 0 && memory_row_inked());
  delete app;
}

// A battery the test sets, and a last run that may have ended in a crash.
class TestPower : public Power {
 public:
  int battery_percent() override { return level; }
  bool charging() override { return plugged; }
  void sleep() override { ++sleeps; }
  void set_wake_timer(uint32_t seconds) override { wake_after = seconds; }
  bool woke_by_timer() override { return timer; }
  bool after_crash() override { return crashed; }
  int level = 50;
  bool plugged = false;
  int sleeps = 0;
  uint32_t wake_after = 7;
  bool timer = false;
  bool crashed = false;
};

void test_app_empty_battery() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  TestPower power;
  TestClock clock;
  FakeUpdater updater;
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/update.txt");
  StdioStorage card("test-card");
  card.write("vellum/state.txt", "reader\n", 7);
  Board board{"test", panel, input, power, nullptr, &clock, &card, Rotation::R90, buf.data()};
  board.updater = &updater;

  // Woken with an empty battery: straight back to sleep, asking for a charge.
  power.level = 2;
  auto* app = new App(board);
  app->start();
  CHECK(power.sleeps == 1);
  CHECK(power.wake_after == 0);                                 // no radio at night on an empty battery
  CHECK(panel.full == 1);                                       // the sleep screen
  CHECK(read_all("test-card/vellum/state.txt") == "reader\n");  // once charged, back into the book
  press(*app, Key::Power);                                      // still empty: sleeps again
  CHECK(power.sleeps == 2);
  power.plugged = true;  // charging: it wakes
  press(*app, Key::Power);
  CHECK(power.sleeps == 2);
  delete app;

  // Running down while in use: the next press sleeps.
  power.plugged = false;
  power.level = 5;
  app = new App(board);
  app->start();
  press(*app, Key::Next);
  CHECK(power.sleeps == 2);
  power.level = 1;
  press(*app, Key::Next);
  CHECK(power.sleeps == 3 && power.wake_after == 0);
  delete app;

  // Woken at night with little charge: no update check, back to sleep.
  power.level = 15;
  power.timer = true;
  app = new App(board);
  app->start();
  CHECK(updater.checks == 0 && power.sleeps == 4);
  delete app;
  power.level = 60;
  app = new App(board);
  app->start();
  CHECK(updater.checks == 1 && updater.installs == 1);  // enough charge: it looks, and installs
  delete app;
  std::remove("test-card/vellum/update.txt");
  std::remove("test-card/vellum/state.txt");
}

void test_app_after_crash() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  TestPower power;
  StdioStorage card("test-card");
  const char* saved = "menu_font=Atkinson Hyperlegible Next\n";
  card.write("vellum/settings.txt", saved, static_cast<uint32_t>(std::strlen(saved)));
  card.write("vellum/state.txt", "reader\n", 7);
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};

  // A normal start: the chosen font, and back into the book.
  auto* app = new App(board);
  app->start();
  CHECK(std::string(ui::menu_fonts().label->family) == "Atkinson Hyperlegible Next");
  app->handle(key_event(InputEvent::Type::KeyDown, Key::Power));
  app->timeout();  // held: sleep, from the book
  CHECK(read_all("test-card/vellum/state.txt") == "reader\n");
  delete app;

  // After a crash: home, with Literata; the settings stay as they were.
  power.crashed = true;
  app = new App(board);
  app->start();
  CHECK(ui::menu_fonts().label == &fonts::literata_16);
  CHECK(read_all("test-card/vellum/settings.txt") == saved);
  app->handle(key_event(InputEvent::Type::KeyDown, Key::Power));
  app->timeout();
  CHECK(read_all("test-card/vellum/state.txt") == "home\n");  // slept from home, not the book
  press(*app, Key::Power);                                    // wake
  // Choosing a menu font again uses it at once.
  press(*app, Key::Prev);  // Settings, the last link
  press(*app, Key::Power);
  press(*app, Key::Next);  // Menu font: the next after Atkinson
  press(*app, Key::Power);
  CHECK(std::string(ui::menu_fonts().label->family) == "EB Garamond");
  delete app;
  std::remove("test-card/vellum/settings.txt");
  std::remove("test-card/vellum/state.txt");
  ui::set_menu_fonts(literata_menu());
}

// Two readers on one desk: packets go into the other's queue, and every
// `drop_every`-th one is lost on the way.
struct DeskRadio {
  struct End : Peers {
    DeskRadio* desk = nullptr;
    int index = 0;
    const char* label = "";
    std::vector<PeerPacket> inbox;
    bool start() override { return true; }
    void stop() override {}
    const char* name() override { return label; }
    bool send(const PeerAddress* to, const void* data, uint32_t len) override {
      CHECK(len <= PeerPacket::kMaxSize);
      if (desk->drop_every > 0 && ++desk->sent % desk->drop_every == 0) return true;
      End& other = desk->ends[1 - index];
      if (to != nullptr && !(*to == other.address())) return true;
      PeerPacket p;
      p.from = address();
      p.size = static_cast<uint16_t>(len);
      std::memcpy(p.data, data, len);
      other.inbox.push_back(p);
      return true;
    }
    bool receive(PeerPacket& out) override {
      if (inbox.empty()) return false;
      out = inbox.front();
      inbox.erase(inbox.begin());
      return true;
    }
    PeerAddress address() const {
      PeerAddress a;
      a.bytes[5] = static_cast<uint8_t>(index + 1);
      return a;
    }
  };
  End ends[2];
  int drop_every = 0;
  int sent = 0;
  DeskRadio() {
    for (int i = 0; i < 2; ++i) {
      ends[i].desk = this;
      ends[i].index = i;
    }
    ends[0].label = "Vellum-AAAA";
    ends[1].label = "Vellum-BBBB";
  }
};

// Delivers packets until both sides are quiet, then ticks once; repeats.
void run_desk(DeskRadio& desk, PeerSender& tx, PeerReceiver& rx, int rounds) {
  for (int r = 0; r < rounds && tx.active(); ++r) {
    PeerPacket p;
    bool moved = true;
    while (moved) {
      moved = false;
      while (desk.ends[1].receive(p)) moved = true, rx.on_packet(p);
      while (desk.ends[0].receive(p)) moved = true, tx.on_packet(p);
    }
    if (tx.state() == PeerSender::State::Looking && tx.reader_count() > 0) tx.send_to(0);
    tx.tick();
    rx.tick();
  }
}

void test_peer_transfer() {
  CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u);
  StdioStorage card_a("test-card");
  StdioStorage card_b("test-card-b");
  std::string book;
  for (int i = 0; book.size() < 20000; ++i) book += "Page " + std::to_string(i) + " of a book sent across. ";
  CHECK(card_a.write("books/Sent Over.epub", book.data(), static_cast<uint32_t>(book.size())));

  for (int drop : {0, 7, 3}) {
    std::remove("test-card-b/books/Sent Over.epub");
    DeskRadio desk;
    desk.drop_every = drop;
    PeerSender tx;
    PeerReceiver rx;
    rx.begin(desk.ends[1], card_b);
    CHECK(tx.begin(desk.ends[0], card_a, "books/Sent Over.epub"));
    CHECK(tx.state() == PeerSender::State::Looking);
    run_desk(desk, tx, rx, 2000);
    if (tx.state() != PeerSender::State::Sent) std::printf("drop %d: state %d\n", drop, static_cast<int>(tx.state()));
    CHECK(tx.state() == PeerSender::State::Sent);
    CHECK(std::string(tx.target_name()) == "Vellum-BBBB");
    CHECK(rx.received() == 1 && std::string(rx.last()) == "Sent Over.epub");
    CHECK(std::string(rx.sender()) == "Vellum-AAAA");
    CHECK(read_all("test-card-b/books/Sent Over.epub") == book);
    CHECK(rx.state() == PeerReceiver::State::Listening);
  }

  // Nobody listening: the sender gives up after a while.
  {
    DeskRadio desk;
    PeerSender tx;
    CHECK(tx.begin(desk.ends[0], card_a, "books/Sent Over.epub"));
    for (int i = 0; i < 400 && tx.active(); ++i) tx.tick();
    CHECK(tx.state() == PeerSender::State::Failed && tx.error() == PeerSender::Error::NoAnswer);
  }
  // The receiver goes away halfway: the sender fails, and the receiver
  // throws its half of the book away.
  {
    std::remove("test-card-b/books/Sent Over.epub");
    DeskRadio desk;
    PeerSender tx;
    PeerReceiver rx;
    rx.begin(desk.ends[1], card_b);
    CHECK(tx.begin(desk.ends[0], card_a, "books/Sent Over.epub"));
    PeerPacket p;
    for (int i = 0; i < 8; ++i) {  // about six pieces of fifteen
      while (desk.ends[1].receive(p)) rx.on_packet(p);
      while (desk.ends[0].receive(p)) tx.on_packet(p);
      if (tx.state() == PeerSender::State::Looking && tx.reader_count() > 0) tx.send_to(0);
      if (tx.state() == PeerSender::State::Looking) tx.tick();
    }
    CHECK(rx.busy() && rx.percent() > 0 && rx.percent() < 100);
    for (int i = 0; i < 60; ++i) {
      tx.tick();
      rx.tick();
      desk.ends[1].inbox.clear();  // out of range
    }
    CHECK(tx.state() == PeerSender::State::Failed && tx.error() == PeerSender::Error::NoAnswer);
    CHECK(!rx.busy() && rx.received() == 0);
    CHECK(read_all("test-card-b/books/Sent Over.epub").empty());
    CHECK(read_all("test-card-b/books/Sent Over.epub.part").empty());
  }
  // Not a book: refused.
  {
    DeskRadio desk;
    PeerSender tx;
    PeerReceiver rx;
    rx.begin(desk.ends[1], card_b);
    CHECK(card_a.write("books/notes.pdf", "hi", 2));
    CHECK(tx.begin(desk.ends[0], card_a, "books/notes.pdf"));
    run_desk(desk, tx, rx, 50);
    CHECK(tx.state() == PeerSender::State::Failed && tx.error() == PeerSender::Error::Refused);
  }
  std::remove("test-card/books/Sent Over.epub");
  std::remove("test-card/books/notes.txt");
}

// Two whole apps: one sends the book it is reading, the other receives it
// on its "Add books" screen.
void test_app_send_book() {
  std::remove("test-card/vellum/state.txt");
  std::remove("test-card-b/vellum/state.txt");
  std::remove("test-card-b/vellum/library.txt");
  std::remove("test-card-b/books/sample.epub");
  StdioStorage card_a("test-card");
  StdioStorage card_b("test-card-b");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  CHECK(card_a.write("books/sample.epub", book.data(), static_cast<uint32_t>(book.size())));
  const char* progress = "books/sample.epub\t0\t0\t0\t0\t1\n";  // the book read last
  CHECK(card_a.write("vellum/progress.txt", progress, static_cast<uint32_t>(std::strlen(progress))));

  DeskRadio desk;
  desk.drop_every = 5;
  std::vector<uint8_t> buf_a(Framebuffer::bytes_for(kW, kH)), buf_b(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel_a, panel_b;
  NoInput input;
  FixedPower power;
  FakeTransfer transfer;
  Board board_a{"a",     panel_a,       input,        power,   nullptr,      nullptr,
                &card_a, Rotation::R90, buf_a.data(), nullptr, &desk.ends[0]};
  Board board_b{"b",     panel_b,       input,        power,     nullptr,      nullptr,
                &card_b, Rotation::R90, buf_b.data(), &transfer, &desk.ends[1]};
  auto* a = new App(board_a);
  auto* b = new App(board_b);
  a->start();
  b->start();

  for (int i = 0; i < 4; ++i) press(*b, Key::Next);  // the current book, 2 more, All books: Add books
  press(*b, Key::Power);
  CHECK(transfer.starts == 1);

  press(*a, Key::Power);  // open the book
  press(*a, Key::Power);  // contents: back, bookmark, light, look up, turn pages, send
  for (int i = 0; i < 5; ++i) press(*a, Key::Next);
  const int full = panel_a.full;
  press(*a, Key::Power);
  CHECK(panel_a.full == full + 1);  // the send screen
  CHECK(a->wait_ms() == peer::kTickMs);

  // Run both until quiet; while looking, choose the first reader found.
  InputEvent peer_event;
  peer_event.type = InputEvent::Type::Peer;
  bool chose = false;
  for (int round = 0; round < 3000; ++round) {
    bool moved = false;
    while (!desk.ends[1].inbox.empty() || !desk.ends[0].inbox.empty()) {
      moved = true;
      if (!desk.ends[1].inbox.empty()) b->handle(peer_event);
      if (!desk.ends[0].inbox.empty()) a->handle(peer_event);
    }
    if (!chose && panel_a.fast > 0) {
      press(*a, Key::Power);  // a reader is listed: send to it
      chose = true;
      continue;
    }
    if (moved) continue;
    const bool a_ticks = a->wait_ms() == peer::kTickMs;
    if (a_ticks) a->timeout();
    if (b->wait_ms() == peer::kTickMs) b->timeout();
    if (!a_ticks && chose) break;
  }
  CHECK(chose);
  CHECK(read_all("test-card-b/books/sample.epub") == book);
  CHECK(a->wait_ms() != peer::kTickMs);  // done: back to the usual idle time

  const int full_a = panel_a.full;
  press(*a, Key::Power);  // sent: back to the book
  CHECK(panel_a.full == full_a + 1);
  press(*b, Key::Home);  // done adding: the library now has the book
  CHECK(transfer.stops == 1);
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card_b, epub);
  bool found = false;
  for (int i = 0; i < lib.count(); ++i) found |= std::string(lib.entry(i).path) == "books/sample.epub";
  CHECK(found);
  delete a;
  delete b;
  std::remove("test-card-b/books/sample.epub");
}

InputEvent timed(InputEvent::Type type, int x, int y, uint32_t ms) {
  InputEvent ev = touch_at(type, x, y);
  ev.ms = ms;
  return ev;
}

void test_long_press() {
  using T = InputEvent::Type;
  using K = Gesture::Kind;
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  TouchTracker t;
  // A board that keeps reporting a resting finger: the long press comes
  // while it is still down, once; its release is nothing.
  t.feed(timed(T::TouchDown, 200, 300, 1000), f.fb);
  CHECK(t.feed(timed(T::TouchMove, 202, 301, 1300), f.fb).kind == K::None);
  const Gesture g = t.feed(timed(T::TouchMove, 203, 302, 1650), f.fb);
  CHECK(g.kind == K::LongPress && g.x == 200 && g.y == 300);
  CHECK(t.feed(timed(T::TouchMove, 203, 302, 1700), f.fb).kind == K::None);
  CHECK(t.feed(timed(T::TouchUp, 203, 302, 1800), f.fb).kind == K::None);
  // A quick touch is still a tap; a slow one released still is a long press.
  t.feed(timed(T::TouchDown, 200, 300, 5000), f.fb);
  CHECK(t.feed(timed(T::TouchUp, 200, 300, 5150), f.fb).kind == K::Tap);
  t.feed(timed(T::TouchDown, 200, 300, 6000), f.fb);
  CHECK(t.feed(timed(T::TouchUp, 200, 300, 6700), f.fb).kind == K::LongPress);
  // A slow swipe is a swipe.
  t.feed(timed(T::TouchDown, 400, 300, 7000), f.fb);
  CHECK(t.feed(timed(T::TouchMove, 300, 300, 7500), f.fb).kind == K::None);
  CHECK(t.feed(timed(T::TouchUp, 150, 300, 8000), f.fb).kind == K::SwipeLeft);
  // Without times (the simulators), the host asks once the finger rested.
  t.feed(touch_at(T::TouchDown, 100, 100), f.fb);
  CHECK(t.holding());
  CHECK(t.hold().kind == K::LongPress);
  CHECK(!t.holding());
  CHECK(t.feed(touch_at(T::TouchUp, 100, 100), f.fb).kind == K::None);
  // A finger that moved is not held.
  t.feed(touch_at(T::TouchDown, 100, 100), f.fb);
  t.feed(touch_at(T::TouchMove, 180, 100), f.fb);
  CHECK(!t.holding() && t.hold().kind == K::None);
  t.feed(touch_at(T::TouchUp, 180, 100), f.fb);
}

void test_word_boxes() {
  const Book& book = books::alice;
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  f.fb.clear(Color::White);
  const PageStyle style{&fonts::literata_26, {28, 70, 424, 660}, 26, &hyphenation::en_us};
  PageWords drawn;
  PageWords measured;
  const TextPos next = typeset_page(style, book.paragraphs, book.paragraph_count, {}, &f.fb, &drawn);
  CHECK(typeset_page(style, book.paragraphs, book.paragraph_count, {}, nullptr, &measured) == next);
  CHECK(drawn.count > 50 && drawn.count == measured.count);
  int hyphens = 0;
  for (int i = 0; i < drawn.count; ++i) {
    const WordBox& b = drawn.words[i];
    CHECK(b.x == measured.words[i].x && b.y == measured.words[i].y && b.begin == measured.words[i].begin);
    // Inside the text area, in reading order, and exactly the word's bytes.
    CHECK(b.x >= style.area.x && b.x + b.w <= style.area.x + style.area.w + 2 && b.y >= style.area.y);
    CHECK(b.w > 0 && b.h > 0 && b.end > b.begin.offset);
    if (i > 0) CHECK(drawn.words[i - 1].begin < b.begin);
    const std::string_view word(book.paragraphs[b.begin.paragraph] + b.begin.offset, b.end - b.begin.offset);
    CHECK(word.find(' ') == std::string_view::npos);
    // The box is where the word is: ink inside it, and it is found there.
    bool ink = false;
    for (int y = b.y; y < b.y + b.h && !ink; ++y) {
      for (int x = b.x; x < b.x + b.w && !ink; ++x) ink = f.fb.pixel(x, y) == Color::Black;
    }
    CHECK(ink);
    CHECK(drawn.at(b.x + b.w / 2, b.y + b.h / 2) == i);
    if (b.hyphen) {
      ++hyphens;
      CHECK(i + 1 < drawn.count && drawn.words[i + 1].begin.offset == b.end);
      CHECK(drawn.words[i + 1].baseline > b.baseline);
    }
  }
  CHECK(hyphens > 0);           // "noth-ing" on the first page
  CHECK(drawn.at(2, 2) == -1);  // the margin is no word
  // Near a word on its line still finds it; far off does not.
  const WordBox& first = drawn.words[0];
  CHECK(drawn.at(first.x - 6, first.y + 4) == 0);
  CHECK(drawn.at(first.x - 40, first.y + 4) == -1);

  // The reader joins a hyphenated word and gives passages as text.
  BuiltinDocument doc;
  doc.set(book);
  Reader reader;
  Settings s;
  CHECK(reader.open(doc, 1, reading_style(s), {}, f.fb));
  const PageWords& w = reader.words();
  CHECK(w.count > 50);
  int nothing = -1;
  for (int i = 0; i < w.count && nothing < 0; ++i) {
    char text[64];
    reader.word_text(i, text, sizeof(text));
    if (std::string(text) == "nothing") nothing = i;
  }
  CHECK(nothing >= 0 && w.words[nothing].hyphen);  // "noth-" + "ing", whole
  char passage[64];
  reader.passage(w.words[0].begin, {0, w.words[3].end}, passage, sizeof(passage));
  CHECK(std::string(passage) == "Alice was beginning to");
  reader.passage({0, 0}, {2, 10}, passage, 24);
  CHECK(std::string(passage).ends_with("\xE2\x80\xA6") && std::strlen(passage) < 24);
}

void test_word_forms() {
  char forms[12][dict::kFormBytes];
  auto has = [&](int n, const char* s) {
    for (int i = 0; i < n; ++i) {
      if (std::string(forms[i]) == s) return true;
    }
    return false;
  };
  int n = dict::word_forms("\xE2\x80\x9CRunning,\xE2\x80\x9D", "en", forms, 12);  // “Running,”
  CHECK(n >= 3 && std::string(forms[0]) == "Running" && std::string(forms[1]) == "running" && has(n, "run"));
  n = dict::word_forms("studies", "en-GB", forms, 12);
  CHECK(has(n, "study"));
  n = dict::word_forms("loved.", "en", forms, 12);
  CHECK(has(n, "love") && has(n, "lov"));
  n = dict::word_forms("stopped", "en", forms, 12);
  CHECK(has(n, "stop"));
  n = dict::word_forms("making", "en", forms, 12);
  CHECK(has(n, "make"));
  n = dict::word_forms("cats", "en", forms, 12);
  CHECK(has(n, "cat"));
  n = dict::word_forms("Alice\xE2\x80\x99s", "en", forms, 12);  // Alice’s
  CHECK(has(n, "alice"));
  n = dict::word_forms("(rabbit-hole)", "en", forms, 12);
  CHECK(n >= 1 && std::string(forms[0]) == "rabbit-hole");
  // Lithuanian: as written and lowercase, with its own letters.
  n = dict::word_forms("\xE2\x80\x9E\xC4\x84\xC5\xBEuolas,", "lt", forms, 12);  // „Ąžuolas,
  CHECK(n == 2 && std::string(forms[0]) == "\xC4\x84\xC5\xBEuolas" && std::string(forms[1]) == "\xC4\x85\xC5\xBEuolas");
  n = dict::word_forms("namas", "lt", forms, 12);
  CHECK(n == 1);                                                  // already lowercase; no English endings
  CHECK(dict::word_forms("\xE2\x80\x94", "en", forms, 12) == 0);  // a dash alone is no word

  char lower[64];
  dict::to_lower("\xC4\x8CIU\xC5\xBDINYS \xC4\x96", lower, sizeof(lower));  // ČIUŽINYS Ė
  CHECK(std::string(lower) == "\xC4\x8Diu\xC5\xBEinys \xC4\x97");

  char text[128];
  dict::markup_to_text("<b>bank</b><br>1. The  land &amp; river.<br/>2. <i>money</i>", text, sizeof(text));
  CHECK(std::string(text) == "bank\n1. The land & river.\n2. money");
  dict::markup_to_text("<k>word</k>A <c c=\"red\">meaning</c>&nbsp;here", text, sizeof(text));
  CHECK(std::string(text) == "A meaning here");
  dict::markup_to_text("a < b\nnext  line", text, sizeof(text), true);
  CHECK(std::string(text) == "a < b\nnext line");

  char lang[8];
  dict::language_of("dictionaries/en/WordNet", lang, sizeof(lang));
  CHECK(std::string(lang) == "en");
  dict::language_of("dictionaries/stardict-lt-en-2.4.2/lt-en", lang, sizeof(lang));
  CHECK(std::string(lang) == "lt");
  dict::language_of("dictionaries/Websters 1913", lang, sizeof(lang));
  CHECK(std::string(lang).empty());
}

void put_be32(std::string& s, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) s += static_cast<char>((v >> shift) & 0xFF);
}

// A StarDict dictionary of `words` (in StarDict order) written to the card.
void write_dictionary(StdioStorage& card, const std::string& base, const std::string& ifo_extra,
                      const std::vector<std::pair<std::string, std::string>>& words) {
  std::string idx, data;
  for (const auto& [word, body] : words) {
    idx += word;
    idx += '\0';
    put_be32(idx, static_cast<uint32_t>(data.size()));
    put_be32(idx, static_cast<uint32_t>(body.size()));
    data += body;
  }
  const std::string ifo = "StarDict's dict ifo file\nversion=2.4.2\nwordcount=" + std::to_string(words.size()) +
                          "\nidxfilesize=" + std::to_string(idx.size()) + "\n" + ifo_extra;
  CHECK(card.write((base + ".ifo").c_str(), ifo.data(), static_cast<uint32_t>(ifo.size())));
  CHECK(card.write((base + ".idx").c_str(), idx.data(), static_cast<uint32_t>(idx.size())));
  CHECK(card.write((base + ".dict").c_str(), data.data(), static_cast<uint32_t>(data.size())));
}

void copy_sample_dictionary(StdioStorage& card, const char* folder) {
  for (const char* ext : {".ifo", ".idx", ".dict.dz"}) {
    const std::string data = read_all((std::string(VELLUM_TEST_DATA "/dictionary/en-sample") + ext).c_str());
    CHECK(!data.empty());
    CHECK(card.write((std::string(folder) + "/en-sample" + ext).c_str(), data.data(),
                     static_cast<uint32_t>(data.size())));
  }
}

void test_stardict() {
  StdioStorage card("test-card-dict");
  std::filesystem::remove_all("test-card-dict/vellum/dictionaries");  // indexes from earlier runs
  // The checked-in English sample (a dictzip), in a folder of its own.
  copy_sample_dictionary(card, "dictionaries/en");
  // A Lithuanian one whose fields name their types, a binary one among them.
  std::string namas = "mPastatas gyventi.";
  namas += '\0';
  namas += "W";
  put_be32(namas, 3);
  namas += "abc";
  namas += "h<i>daiktavardis</i>";
  namas += '\0';
  write_dictionary(card, "dictionaries/lt-sample", "bookname=Pavyzdinis\n",
                   {{"namas", namas}, {"\xC4\x85\xC5\xBEuolas", std::string("mDidelis medis.") + '\0'}});
  // A large one, so the index keeps only every few words.
  std::vector<std::pair<std::string, std::string>> many;
  for (int i = 0; i < 20000; ++i) {
    char word[16];
    std::snprintf(word, sizeof(word), "w%05d", i);
    many.push_back({word, "word " + std::to_string(i)});
  }
  write_dictionary(card, "dictionaries/big-test", "bookname=Big\nsametypesequence=m\n", many);

  Dictionaries d;
  d.scan(&card);
  CHECK(d.count() == 3);
  Dictionaries::Result r;
  char text[512];
  int slow = 0;
  auto count_slow = [](void* ctx) { ++*static_cast<int*>(ctx); };
  CHECK(d.lookup("bank,", "en", "lt", r, text, sizeof(text), count_slow, &slow));
  CHECK(std::string(r.headword) == "bank" && std::string(r.dictionary) == "Sample English");
  CHECK(std::string(text) == "bank\n1. The land beside a river.\n2. A place that keeps money.");
  CHECK(d.lookup("Rabbit", "en", "lt", r, text, sizeof(text)));
  CHECK(std::string(text) == "A small animal with long ears <Leporidae>.");
  CHECK(d.lookup("Alice", "en", "lt", r, text, sizeof(text)) && std::string(r.headword) == "Alice");
  for (auto [asked, found] : {std::pair{"studies", "study"},
                              {"loved", "love"},
                              {"running", "run"},
                              {"Tired", "tired"},
                              {"conversations", "conversation"},
                              {"sister\xE2\x80\x99s", "sister"}}) {
    CHECK(d.lookup(asked, "en-US", "lt", r, text, sizeof(text)) && std::string(r.headword) == found);
  }
  CHECK(std::string(text) == "A girl or woman with the same parents as another person.");  // across chunks
  // A word in none of them: each is indexed on the way, the first time.
  slow = 0;
  CHECK(!d.lookup("zebra", "en", "lt", r, text, sizeof(text), count_slow, &slow));
  CHECK(slow == 2);  // the Lithuanian one and the large one; the English one already was
  // Lithuanian: the book's language picks the dictionary; fields in order.
  CHECK(d.lookup("\xC4\x84\xC5\xBEuolas", "lt", "en", r, text, sizeof(text)));
  CHECK(std::string(r.dictionary) == "Pavyzdinis" && std::string(text) == "Didelis medis.");
  CHECK(d.lookup("Namas.", "lt", "en", r, text, sizeof(text)));
  CHECK(std::string(text) == "Pastatas gyventi.\ndaiktavardis");
  // The large one, through its sparse index.
  CHECK(d.lookup("w12345", "xx", "en", r, text, sizeof(text)));
  CHECK(std::string(text) == "word 12345");
  CHECK(d.lookup("w00000", "xx", "en", r, text, sizeof(text)) && std::string(text) == "word 0");
  CHECK(d.lookup("W19999", "xx", "en", r, text, sizeof(text)) && std::string(text) == "word 19999");
  CHECK(!d.lookup("w20000", "xx", "en", r, text, sizeof(text)));
  Dictionaries again;
  again.scan(&card);
  slow = 0;
  CHECK(again.lookup("w04321", "xx", "en", r, text, sizeof(text), count_slow, &slow));
  CHECK(std::string(text) == "word 4321" && slow == 0);  // the indexes are read from the card
  // No card, no dictionaries.
  Dictionaries none;
  none.scan(nullptr);
  CHECK(none.count() == 0 && !none.lookup("bank", "en", "", r, text, sizeof(text)));
}

void test_highlights_store() {
  StdioStorage card("test-card-dict");
  std::remove("test-card-dict/vellum/highlights.txt");
  Highlights h;
  h.load(card);
  CHECK(h.count() == 0);
  Highlight a;
  std::snprintf(a.path, sizeof(a.path), "books/a.epub");
  std::snprintf(a.title, sizeof(a.title), "A \"Book\"");
  a.begin = {2, {10, 4}};
  a.end = {11, 20};
  a.percent = 40;
  std::snprintf(a.date, sizeof(a.date), "2026-10-04 21:30");
  std::snprintf(a.text, sizeof(a.text), "first\tpassage\nhere");
  Highlight b = a;
  b.begin = {1, {3, 0}};
  b.end = {3, 9};
  std::snprintf(b.text, sizeof(b.text), "earlier");
  Highlight other = a;
  std::snprintf(other.path, sizeof(other.path), "books/b.epub");
  CHECK(h.add(a) == 0 && h.add(b) == 1 && h.add(other) == 2);
  CHECK(h.at(0).id == 1 && h.at(1).id == 2 && h.at(2).id == 3);
  CHECK(std::string(h.at(0).text) == "first passage here");  // one line on the card
  CHECK(h.set_note(2, "a note\twith\nbreaks"));
  CHECK(!h.set_note(99, "nothing"));
  CHECK(h.save(card));

  Highlights loaded;
  loaded.load(card);
  CHECK(loaded.count() == 3);
  const Highlight& x = loaded.at(1);
  CHECK(x.id == 2 && std::string(x.path) == "books/a.epub" && std::string(x.title) == "A \"Book\"");
  CHECK(x.begin.chapter == 1 && x.begin.text == (TextPos{3, 0}) && x.end == (TextPos{3, 9}));
  CHECK(x.percent == 40 && std::string(x.date) == "2026-10-04 21:30" && std::string(x.note) == "a note with breaks");
  int order[4];
  CHECK(loaded.of_book("books/a.epub", -1, order, 4) == 2 && order[0] == 1 && order[1] == 0);  // reading order
  CHECK(loaded.of_book("books/a.epub", 2, order, 4) == 1 && order[0] == 0);
  CHECK(loaded.find(3) == 2 && loaded.find(7) == -1);
  loaded.remove(0);
  Highlight c = a;
  CHECK(loaded.add(c) == 2 && loaded.at(2).id == 4);  // ids are never reused

  std::string json;
  loaded.write_json([](void* ctx, const char* s, size_t n) { static_cast<std::string*>(ctx)->append(s, n); }, &json);
  CHECK(json.starts_with("[{\"id\":2,\"book\":\"books/a.epub\",\"title\":\"A \\\"Book\\\"\",\"chapter\":1"));
  CHECK(json.find("\"note\":\"a note with breaks\"") != std::string::npos);
  CHECK(json.ends_with("}]") && std::count(json.begin(), json.end(), '{') == 3);
  // A long passage is cut at a whole character.
  Highlight longer = a;
  std::string many;
  for (int i = 0; i < 200; ++i) many += "\xC4\x85";
  std::snprintf(longer.text, sizeof(longer.text), "%s", "");
  std::snprintf(longer.note, sizeof(longer.note), "%s", "");
  CHECK(loaded.add(longer) >= 0);
  CHECK(loaded.set_note(loaded.at(loaded.count() - 1).id, many.c_str()));
  const std::string note = loaded.at(loaded.count() - 1).note;
  CHECK(note.size() == 238 && note.size() % 2 == 0);
  std::remove("test-card-dict/vellum/highlights.txt");
}

// Where the reader puts `word` on the first page of Alice, with default settings.
WordBox alice_word(const char* word) {
  static std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  static Framebuffer fb(buf.data(), kW, kH);
  fb.set_rotation(Rotation::R90);
  static BuiltinDocument doc;
  doc.set(books::alice);
  static Reader reader;
  reader.open(doc, 1, reading_style(Settings{}), {}, fb);
  const PageWords& w = reader.words();
  for (int i = 0; i < w.count; ++i) {
    char text[64];
    reader.word_text(i, text, sizeof(text));
    if (std::string(text) == word) return w.words[i];
  }
  CHECK(false);
  return {};
}

void hold(App& app, int x, int y) {
  app.handle(touch_at(InputEvent::Type::TouchDown, x, y));
  CHECK(app.wait_ms() == TouchTracker::kLongPressMs);  // the host waits that long, then:
  app.timeout();
  app.handle(touch_at(InputEvent::Type::TouchUp, x, y));
}

// A row of the screen fully black from x0 to x1, somewhere in rows y0..y1.
bool black_run(const Framebuffer& fb, int x0, int x1, int y0, int y1) {
  for (int y = y0; y <= y1; ++y) {
    bool all = true;
    for (int x = x0; x < x1 && all; ++x) all = fb.pixel(x, y) == Color::Black;
    if (all) return true;
  }
  return false;
}

void test_app_words() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer screen(buf.data(), kW, kH);
  screen.set_rotation(Rotation::R90);
  CountingPanel panel;
  NoInput input;
  SleepyPower power;
  StdioStorage card("test-card-words");
  for (const char* p : {"test-card-words/vellum/state.txt", "test-card-words/vellum/highlights.txt",
                        "test-card-words/vellum/progress.txt", "test-card-words/vellum/settings.txt"}) {
    std::remove(p);
  }
  std::filesystem::remove_all("test-card-words/vellum/dictionaries");  // indexes from earlier runs
  copy_sample_dictionary(card, "dictionaries");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);  // open Alice, the first book

  // Holding "sister" selects it and shows its meaning in a panel below.
  const WordBox sister = alice_word("sister");
  int fast = panel.fast;
  hold(*app, sister.x + sister.w / 2, sister.y + sister.h / 2);
  CHECK(panel.fast == fast + 2);  // "Preparing the dictionary…" first: no index yet
  CHECK(black_run(screen, sister.x, sister.x + sister.w, sister.y, sister.y + sister.h));  // inverted
  CHECK(black_run(screen, 0, kH, 420, kW - 1));                                            // the panel's edge
  // Highlight, then tap where the passage ends: it is saved and underlined.
  tap(*app, 60, kW - 38);
  const WordBox end = alice_word("do:");
  tap(*app, end.x + end.w / 2, end.y + end.h / 2);
  std::string saved = read_all("test-card-words/vellum/highlights.txt");
  CHECK(saved.starts_with("1\tbuiltin:Alice"));
  CHECK(saved.find("\t0:0.") != std::string::npos);
  CHECK(saved.find("\tsister on the bank, and of having nothing to do:\t") != std::string::npos);
  CHECK(!black_run(screen, 0, kH, 420, kW - 1));  // the panel is gone
  CHECK(black_run(screen, sister.x, sister.x + sister.w, sister.baseline + 2, sister.baseline + 9));
  CHECK(black_run(screen, end.x, end.x + end.w, end.baseline + 2, end.baseline + 9));
  const WordBox alice = alice_word("Alice");
  CHECK(!black_run(screen, alice.x, alice.x + alice.w, alice.baseline + 2, alice.baseline + 9));

  // Contents lists it; its row opens the page.
  const int full = panel.full;
  press(*app, Key::Power);  // rows: back, bookmark, light, look up, turn pages, add to shelf, HIGHLIGHTS, ...
  for (int i = 0; i < 6; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(panel.full == full + 2);  // contents, then the page

  // A note from the phone, read again when Add books closes.
  {
    Highlights phone;
    phone.load(card);
    CHECK(phone.count() == 1 && phone.set_note(phone.at(0).id, "Bored, like me.") && phone.save(card));
  }
  press(*app, Key::Home);
  tap(*app, 240, 640);  // Add books (no Wi-Fi here)
  press(*app, Key::Home);
  press(*app, Key::Power);  // back to Alice
  press(*app, Key::Power);  // contents: the note follows its highlight and is skipped
  // Rows from y = 186, 50 px each: the note (row 8) wears a bar at its left.
  CHECK(screen.pixel(38, 596) == Color::Black && screen.pixel(38, 621) == Color::Black);
  for (int i = 0; i < 7; ++i) press(*app, Key::Next);
  CHECK(screen.pixel(20, 711) == Color::Black);  // row 10, the chapter, is selected
  press(*app, Key::Power);                       // the chapter, not the note
  CHECK(read_all("test-card-words/vellum/highlights.txt").find("\tBored, like me.\n") != std::string::npos);

  // Without touch: look up a word, pick its line and word, highlight to the third.
  press(*app, Key::Power);
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);  // Look up a word: the first line is boxed
  press(*app, Key::Power);  // that line; its first word, "Alice"
  CHECK(black_run(screen, alice.x, alice.x + alice.w, alice.y, alice.y + alice.h));
  press(*app, Key::Power);  // look it up
  CHECK(black_run(screen, 0, kH, 420, kW - 1));
  press(*app, Key::Power);  // Highlight
  press(*app, Key::Next);
  press(*app, Key::Next);
  press(*app, Key::Power);  // to "beginning"
  saved = read_all("test-card-words/vellum/highlights.txt");
  CHECK(count_lines(saved) == 2 && saved.find("\tAlice was beginning\t") != std::string::npos);

  // Holding a highlighted word offers to remove the highlight.
  hold(*app, sister.x + sister.w / 2, sister.y + sister.h / 2);
  press(*app, Key::Power);
  saved = read_all("test-card-words/vellum/highlights.txt");
  CHECK(count_lines(saved) == 1 && saved.find("sister") == std::string::npos);

  // Home closes the panel first, then goes home; page turns wait meanwhile.
  hold(*app, sister.x + sister.w / 2, sister.y + sister.h / 2);
  int shown = panel.fast + panel.full;
  press(*app, Key::Next);  // a definition this short does not scroll: nothing
  CHECK(panel.fast + panel.full == shown);
  press(*app, Key::Home);
  CHECK(panel.fast + panel.full == shown + 1);  // the page again, without the panel
  CHECK(!black_run(screen, 0, kH, 420, kW - 1));
  CHECK(!black_run(screen, sister.x, sister.x + sister.w, sister.y, sister.y + sister.h));
  const int full_before = panel.full;
  press(*app, Key::Home);
  CHECK(panel.full == full_before + 1);  // home
  delete app;
  std::remove("test-card-words/vellum/highlights.txt");
}

void test_dictionary_upload_names() {
  char out[160];
  CHECK(safe_dictionary_name("WordNet.ifo", out, sizeof(out)) && std::string(out) == "WordNet.ifo");
  CHECK(safe_dictionary_name("C:\\x\\en-wordnet.dict.dz", out, sizeof(out)) &&
        std::string(out) == "en-wordnet.dict.dz");
  CHECK(safe_dictionary_name("en.idx", out, sizeof(out)) && safe_dictionary_name("en.DICT", out, sizeof(out)));
  CHECK(!safe_dictionary_name("archive.dz", out, sizeof(out)));
  CHECK(!safe_dictionary_name("book.epub", out, sizeof(out)));
  CHECK(!safe_book_name("en.ifo", out, sizeof(out)));
}

void test_reading_stats() {
  constexpr uint32_t kMin = 60 * 1000;
  const int d = 9773;  // 2026-10-04
  ReadingStats stats;
  // A page read in 30 s, one left open 10 minutes (counted as 2), a last
  // one shown 50 s before leaving.
  stats.start("books/a.epub", 1000);
  CHECK(stats.running());
  stats.turn(31000, d, true);
  stats.turn(31000 + 10 * kMin, d, true);
  stats.stop(31000 + 10 * kMin + 50000, d);
  CHECK(!stats.running());
  CHECK(stats.seconds_between(d, d) == 30 + 120 + 50);
  CHECK(stats.pages_between(d, d) == 2);
  CHECK(stats.find_book("books/a.epub")->seconds == 200);

  // Past midnight the time goes to the next day; going back a page counts
  // time but not a page. Parts of seconds add up.
  stats.start("books/a.epub", 0);
  for (int i = 1; i <= 4; ++i) stats.turn(i * 500, d + 1, i != 2);
  stats.stop(2000, d + 1);
  CHECK(stats.seconds_between(d + 1, d + 1) == 2 && stats.pages_between(d + 1, d + 1) == 3);
  CHECK(stats.seconds_between(d, d + 1) == 202);

  // Without the clock only the book's totals grow.
  stats.start("books/b.epub", 0);
  stats.turn(40000, -1, true);
  stats.stop(40000, -1);
  CHECK(stats.find_book("books/b.epub")->seconds == 40 && stats.find_book("books/b.epub")->pages == 1);
  CHECK(stats.seconds_between(0, 100000) == 202);
  CHECK(std::string(stats.book_by_recent(0).path) == "books/b.epub");

  // Speed: too little to tell, then 60 pages an hour for this book.
  CHECK(stats.pages_per_hour("books/a.epub") == -1);
  CHECK(stats.pages_per_hour() == -1);
  stats.start("books/a.epub", 0);
  for (int i = 1; i <= 20; ++i) stats.turn(i * kMin, d + 2, true);
  stats.stop(20 * kMin, d + 2);
  // 25 pages in 1402 s over the three days.
  CHECK(stats.pages_per_hour("books/a.epub") == 64);
  CHECK(stats.pages_per_hour("books/b.epub") == 64);  // too little of b: all books instead
  CHECK(stats.pages_per_hour() > 50);
  CHECK(minutes_for_pages(12, 60) == 12 && minutes_for_pages(1, 200) == 1 && minutes_for_pages(0, 60) == 0);
  CHECK(minutes_for_pages(5, -1) == -1);

  // Saving waits a few minutes; leaving writes at once (the app's part).
  CHECK(stats.dirty());
  StdioStorage card("test-card");
  std::remove("test-card/vellum/stats.txt");
  CHECK(stats.save(card));
  CHECK(!stats.dirty() && !stats.save_due(20 * kMin + ReadingStats::kSaveMs));
  const std::string file = read_all("test-card/vellum/stats.txt");
  CHECK(file.find("d\t2026-10-04\t0\t200\t2\n") != std::string::npos);

  ReadingStats again;
  again.load(card);
  CHECK(again.book_count() == 2 && again.day_count() == stats.day_count());
  CHECK(again.seconds_between(d, d + 2) == stats.seconds_between(d, d + 2));
  CHECK(again.find_book("books/b.epub")->seconds == 40);
  CHECK(std::string(again.book_by_recent(0).path) == "books/a.epub");
  CHECK(again.pages_per_hour("books/a.epub") == 64);

  // A year on, old days are let go.
  again.start("books/a.epub", 0);
  again.turn(30000, d + 400, true);
  CHECK(again.day_count() == 1 && again.seconds_between(d, d + 2) == 0);
  CHECK(again.find_book("books/a.epub")->seconds > 200);  // the book keeps its totals
  again.stop(30000, d + 400);

  // Too many books: the one read longest ago gives way; its days still count.
  ReadingStats many;
  char path[32];
  for (int i = 0; i <= ReadingStats::kMaxBooks; ++i) {
    std::snprintf(path, sizeof(path), "books/%d.epub", i);
    many.start(path, 0);
    many.turn(60000, d, true);
    many.stop(60000, d);
  }
  CHECK(many.book_count() == ReadingStats::kMaxBooks);
  CHECK(many.find_book("books/0.epub") == nullptr && many.find_book("books/64.epub") != nullptr);
  CHECK(many.seconds_between(d, d) == 60u * (ReadingStats::kMaxBooks + 1));
  std::remove("test-card/vellum/stats.txt");

  char out[32];
  format_duration(Language::English, 12, out, sizeof(out));
  CHECK(std::string(out) == "12 min");
  format_duration(Language::English, 250, out, sizeof(out));
  CHECK(std::string(out) == "4 h 10 min");
  format_duration(Language::Lithuanian, 120, out, sizeof(out));
  CHECK(std::string(out) == "2 val.");
  format_duration(Language::English, 14 * 60 + 40, out, sizeof(out));
  CHECK(std::string(out) == "15 h");
  format_page_count(Language::English, 1, out, sizeof(out));
  CHECK(std::string(out) == "1 page");
  format_page_count(Language::Lithuanian, 12, out, sizeof(out));
  CHECK(std::string(out) == "12 psl.");
}

// The four built-in samples as the chapters of one book, weighed by length.
class SamplerDocument : public Document {
 public:
  const char* language() const override { return "en"; }
  int chapter_count() const override { return 4; }
  uint32_t chapter_weight(int i) const override {
    uint32_t n = 0;
    for (uint32_t p = 0; p < book(i).paragraph_count; ++p)
      n += static_cast<uint32_t>(std::strlen(book(i).paragraphs[p]));
    return n;
  }
  bool load_chapter(int i, Chapter& out) override {
    out = {book(i).paragraphs, book(i).paragraph_count, book(i).chapter_label, book(i).chapter_title};
    return true;
  }
  const TocEntry& toc(int) const override { return toc_; }

 private:
  static const Book& book(int i) { return *builtin_books()[static_cast<size_t>(i)]; }
  TocEntry toc_;
};

void test_reader_pages_left() {
  SamplerDocument doc;
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  Reader reader;
  CHECK(reader.open(doc, 1, reading_style(Settings{}), {}, f.fb));
  CHECK(reader.pages_left_in_chapter() == reader.page_count());
  const int guess = reader.pages_left_in_book();
  int pages = 1;
  while (reader.next()) {
    ++pages;
    CHECK(reader.pages_left_in_book() >= reader.pages_left_in_chapter());
  }
  // The first guess, from one chapter's length, lands near the real count.
  CHECK(guess * 10 > pages * 8 && guess * 10 < pages * 12);
  CHECK(reader.pages_left_in_chapter() == 1 && reader.pages_left_in_book() == 1);
}

// One chapter of `count` paragraphs, numbered so a page tells where it is.
class LongDocument : public Document {
 public:
  explicit LongDocument(int count) {
    for (int i = 0; i < count; ++i) {
      text_.push_back("Paragraph " + std::to_string(i) +
                      " of a chapter much longer than most, set over many pages to see that its end is reached.");
    }
    for (const std::string& t : text_) paragraphs_.push_back(t.c_str());
  }
  const char* language() const override { return "en"; }
  int chapter_count() const override { return 1; }
  uint32_t chapter_weight(int) const override { return static_cast<uint32_t>(paragraphs_.size()) * 100; }
  bool load_chapter(int, Chapter& out) override {
    out = {paragraphs_.data(), static_cast<uint32_t>(paragraphs_.size()), "", ""};
    return true;
  }
  const TocEntry& toc(int) const override { return toc_; }

 private:
  std::vector<std::string> text_;
  std::vector<const char*> paragraphs_;
  TocEntry toc_;
};

void test_reader_long_chapter() {
  // Over 1024 pages in one chapter (the old limit): every page is reached.
  LongDocument doc(9000);
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  Settings large;
  large.text_size = TextSize::ExtraLarge;
  Reader reader;
  CHECK(reader.open(doc, 1, reading_style(large), {}, f.fb));
  CHECK(reader.page_count() > 1024);
  int pages = 1;
  while (reader.next()) ++pages;
  CHECK(pages == reader.page_count());
  CHECK(reader.position().text.paragraph > 8990);

  // Opening another book forgets the first one's pages per length: the
  // guess of pages left matches a fresh reader's.
  SamplerDocument other;
  CHECK(reader.open(other, 2, reading_style(large), {}, f.fb));
  Reader fresh;
  CHECK(fresh.open(other, 2, reading_style(large), {}, f.fb));
  CHECK(reader.pages_left_in_book() == fresh.pages_left_in_book());
}

void test_reader_relayout_rendered_fonts() {
  // A rendered book font changes size or family behind the same pointers:
  // the reader must lay the chapter out again, as a fresh one would.
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  FontLibrary lib;
  lib.scan(nullptr);
  BuiltinDocument doc;
  doc.set(*builtin_books()[0]);
  Settings s;
  BookFonts fonts;
  CHECK(lib.book(lib.find("EB Garamond"), 23, fonts));
  Reader reader;
  CHECK(reader.open(doc, 1, reading_style(s, &fonts), {}, f.fb));
  const int small_pages = reader.page_count();
  s.text_size = TextSize::ExtraLarge;
  CHECK(lib.book(lib.find("EB Garamond"), 34, fonts));
  CHECK(reader.open(doc, 1, reading_style(s, &fonts), {}, f.fb));
  Reader fresh;
  CHECK(fresh.open(doc, 1, reading_style(s, &fonts), {}, f.fb));
  CHECK(reader.page_count() == fresh.page_count() && reader.page_count() > small_pages);
  CHECK(lib.book(lib.find("Atkinson Hyperlegible Next"), 34, fonts));  // same size, another family
  CHECK(reader.open(doc, 1, reading_style(s, &fonts), {}, f.fb));
  Reader fresh2;
  CHECK(fresh2.open(doc, 1, reading_style(s, &fonts), {}, f.fb));
  CHECK(reader.page_count() == fresh2.page_count());
}

// Input with a steady count the test moves forward.
class TimedInput : public InputSource {
 public:
  bool wait(InputEvent&, uint32_t) override { return false; }
  uint32_t now_ms() override { return ms; }
  uint32_t ms = 5000;
};

void test_app_reading_stats() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  TimedInput input;
  FixedPower power;
  TestClock clock;
  StdioStorage card("test-card-stats");  // no books on it: the samples only
  Board board{"test", panel, input, power, nullptr, &clock, &card, Rotation::R90, buf.data()};
  Framebuffer view(buf.data(), kW, kH);
  view.set_rotation(Rotation::R90);
  auto footer = [&] {
    std::string bits;
    for (int y = 800 - 30; y < 800; ++y)
      for (int x = 150; x < 330; ++x) bits += view.pixel(x, y) == Color::Black ? '#' : '.';
    return bits;
  };
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);  // open the current book
  const std::string page_number = footer();
  for (int i = 0; i < 8; ++i) {
    input.ms += 30000;  // a page every half minute
    press(*app, Key::Next);
  }
  CHECK(read_all("test-card-stats/vellum/stats.txt").empty());  // written every few minutes, not yet
  for (int i = 0; i < 5; ++i) {
    input.ms += 30000;
    press(*app, Key::Next);
  }
  CHECK(!read_all("test-card-stats/vellum/stats.txt").empty());
  input.ms += 10 * 60 * 1000;  // left open: only two more minutes count
  press(*app, Key::Home);
  const std::string saved = read_all("test-card-stats/vellum/stats.txt");
  CHECK(saved.find("b\t1\t510\t13\tbuiltin:") != std::string::npos);
  CHECK(saved.find("d\t2026-10-04\t0\t510\t13\n") != std::string::npos);

  // Settings › Reading stats, the ninth row: opens, Power goes back.
  press(*app, Key::Prev);  // Settings, the last link
  press(*app, Key::Power);
  for (int i = 0; i < 8; ++i) press(*app, Key::Next);
  int full = panel.full;
  press(*app, Key::Power);
  CHECK(panel.full == full + 1);
  // Today's weekday sits in a dark pill under the last bar.
  CHECK(view.pixel(28 + 6 * 60 + 30 - 24, 426 - 6) == Color::Black);
  press(*app, Key::Power);
  CHECK(panel.full == full + 2);
  CHECK(view.pixel(17, 430) == Color::Black);  // back on Settings, the Reading stats row highlighted

  // Settings › Reading › Footer: time left in the chapter, then the percent.
  press(*app, Key::Next);  // Device check
  press(*app, Key::Next);  // Home and library view
  press(*app, Key::Next);  // Move finished books
  press(*app, Key::Next);  // Reading
  press(*app, Key::Power);
  for (int i = 0; i < 7; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(read_all("test-card-stats/vellum/settings.txt").find("footer=1") != std::string::npos);
  press(*app, Key::Home);
  press(*app, Key::Home);
  press(*app, Key::Power);  // back in the book
  const std::string chapter_time = footer();
  CHECK(chapter_time != page_number);
  press(*app, Key::Home);
  press(*app, Key::Prev);
  press(*app, Key::Power);
  for (int i = 0; i < 12; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  for (int i = 0; i < 7; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(read_all("test-card-stats/vellum/settings.txt").find("footer=2") != std::string::npos);
  delete app;

  // A restart keeps the totals.
  app = new App(board);
  app->start();
  press(*app, Key::Power);
  input.ms += 60000;
  press(*app, Key::Next);
  press(*app, Key::Home);
  CHECK(read_all("test-card-stats/vellum/stats.txt").find("d\t2026-10-04\t0\t570\t14\n") != std::string::npos);
  delete app;
  for (const char* name : {"stats.txt", "settings.txt", "state.txt", "progress.txt", "library.txt"}) {
    std::remove(("test-card-stats/vellum/" + std::string(name)).c_str());
  }
}

// ---- Free book store -----------------------------------------------------------

// Parses a feed handed over in pieces of `piece` bytes.
OpdsFeed parse_feed(const std::string& text, const char* base, OpdsEntry* entries, int capacity, size_t piece,
                    bool* atom = nullptr) {
  OpdsFeed feed;
  feed.entries = entries;
  feed.capacity = capacity;
  static OpdsParser parser;  // large buffers: not on the stack
  parser.begin(feed, base);
  for (size_t i = 0; i < text.size(); i += piece) parser.feed(text.data() + i, std::min(piece, text.size() - i));
  const bool ok = parser.finish();
  if (atom != nullptr) *atom = ok;
  return feed;
}

void test_opds_gutenberg() {
  static OpdsEntry entries[30];
  const std::string list = read_all(VELLUM_TEST_DATA "/store/gutenberg-popular.opds");
  CHECK(!list.empty());
  const char* base = "https://www.gutenberg.org/ebooks/search.opds/?sort_order=downloads";
  for (size_t piece : {size_t{1}, size_t{7}, size_t{4096}}) {
    bool atom = false;
    const OpdsFeed feed = parse_feed(list, base, entries, 30, piece, &atom);
    CHECK(atom);
    CHECK(feed.count == 10);  // the "Sort by" entries are not books
    CHECK(std::string(entries[0].title) == "Pride and Prejudice");
    CHECK(std::string(entries[0].author) == "Jane Austen");
    CHECK(std::string(entries[0].detail) == "https://www.gutenberg.org/ebooks/1342.opds");
    CHECK(entries[0].epub[0] == '\0');
    CHECK(std::string(entries[3].title) == "Alice's Adventures in Wonderland");
    CHECK(std::string(feed.next) ==
          "https://www.gutenberg.org/ebooks/search.opds/?sort_order=downloads&start_index=26");
  }
  // Fewer places than books: the rest are counted, not kept.
  const OpdsFeed small = parse_feed(list, base, entries, 4, 512);
  CHECK(small.count == 4 && small.dropped == 6);
  // The last page has no next link.
  const OpdsFeed last =
      parse_feed(read_all(VELLUM_TEST_DATA "/store/gutenberg-popular-2.opds"), base, entries, 30, 100);
  CHECK(last.count == 2 && last.next[0] == '\0');
  // Lithuanian titles come through as UTF-8.
  const OpdsFeed lt = parse_feed(read_all(VELLUM_TEST_DATA "/store/gutenberg-lt.opds"), base, entries, 30, 3);
  CHECK(lt.count == 3 && std::string(entries[0].title) == "Anyk\xC5\xA1\xC4\x8Di\xC5\xB3 \xC5\xA1ilelis");

  // One book's own feed: its summary, the best EPUB and its size.
  const std::string book = read_all(VELLUM_TEST_DATA "/store/gutenberg-book.opds");
  for (size_t piece : {size_t{1}, size_t{5}, size_t{9999}}) {
    const OpdsFeed feed = parse_feed(book, "https://www.gutenberg.org/ebooks/1342.opds", entries, 30, piece);
    CHECK(feed.count == 1);  // "By Jane Austen" and "also downloaded" are not books
    const OpdsEntry& e = entries[0];
    CHECK(std::string(e.author) == "Jane Austen");
    CHECK(std::string(e.epub) == "https://www.gutenberg.org/ebooks/1342.epub3.images");
    CHECK(e.size == 24994304);
    CHECK(std::string(e.language) == "en");
    CHECK(std::string(e.summary).rfind("\"Pride and Prejudice\" by Jane Austen is a classic", 0) == 0);
    CHECK(std::string(e.summary).find("Bennet & her sisters") != std::string::npos);
    CHECK(std::string(e.summary).find("England \xE2\x80\x94 with wit") != std::string::npos);
  }
}

void test_opds_standard_ebooks() {
  static OpdsEntry entries[10];
  const std::string text = read_all(VELLUM_TEST_DATA "/store/standard-ebooks-new.atom");
  for (size_t piece : {size_t{1}, size_t{11}, size_t{8192}}) {
    const OpdsFeed feed = parse_feed(text, "https://standardebooks.org/feeds/atom/new-releases", entries, 10, piece);
    CHECK(feed.count == 3);
    CHECK(feed.next[0] == '\0');
    // The compatible EPUB, not the advanced one or the Kobo one.
    CHECK(std::string(entries[0].epub) ==
          "https://standardebooks.org/ebooks/joseph-conrad/the-secret-agent/downloads/"
          "joseph-conrad_the-secret-agent.epub");
    CHECK(entries[0].size == 512340);
    CHECK(std::string(entries[0].summary) ==
          "A shopkeeper in London\xE2\x80\x99s Soho is drawn into an anarchist plot to bomb the Greenwich "
          "Observatory.");
    // CDATA keeps its & and < as they are; a relative link gets the site's address.
    CHECK(std::string(entries[1].summary) == "Lily Bart & the cost of beauty <in> New York society.");
    CHECK(std::string(entries[1].epub) ==
          "https://standardebooks.org/ebooks/edith-wharton/the-house-of-mirth/downloads/"
          "edith-wharton_the-house-of-mirth.epub");
    // Two authors: the first one. No length given: size unknown.
    CHECK(std::string(entries[2].author) == "Fyodor Dostoevsky");
    CHECK(entries[2].size == 0);
  }
}

void test_opds_details() {
  static OpdsEntry entries[4];
  // Escaped HTML loses its markup; ISO-8859-1 turns into UTF-8; comments are skipped.
  const std::string feed_text =
      "<?xml version='1.0' encoding='ISO-8859-1'?><feed xmlns='http://www.w3.org/2005/Atom'>"
      "<!-- <entry><title>Not this</title></entry> -->"
      "<entry><title>Caf\xE9</title><author><name>Dumas, Alexandre</name></author>"
      "<content type='html'>&lt;p&gt;One&lt;/p&gt;&lt;p&gt;two &amp;amp; three&lt;/p&gt;</content>"
      "<link rel='http://opds-spec.org/acquisition/open-access' type='application/epub+zip' href='../b.epub'/>"
      "</entry></feed>";
  bool atom = false;
  const OpdsFeed feed = parse_feed(feed_text, "http://example.org/cat/list.xml?p=1", entries, 4, 3, &atom);
  CHECK(atom && feed.count == 1);
  CHECK(std::string(entries[0].title) == "Caf\xC3\xA9");
  CHECK(std::string(entries[0].author) == "Alexandre Dumas");
  CHECK(std::string(entries[0].summary) == "One two & three");
  CHECK(std::string(entries[0].epub) == "http://example.org/cat/../b.epub");
  // An HTML page is not a feed.
  parse_feed("<!DOCTYPE html><html><body>Sign in</body></html>", "https://x.org/", entries, 4, 8, &atom);
  CHECK(!atom);

  char url[128];
  CHECK(resolve_url("https://a.org/x/y?z=1", "/b", url, sizeof(url)) && std::string(url) == "https://a.org/b");
  CHECK(resolve_url("https://a.org/x/y", "//c.org/d", url, sizeof(url)) && std::string(url) == "https://c.org/d");
  CHECK(resolve_url("https://a.org/x/y", "z", url, sizeof(url)) && std::string(url) == "https://a.org/x/z");
  CHECK(resolve_url("https://a.org", "z", url, sizeof(url)) && std::string(url) == "https://a.org/z");
  CHECK(resolve_url("https://a.org/x", "http://e.org/", url, sizeof(url)) && std::string(url) == "http://e.org/");
  CHECK(!resolve_url("https://a.org/x", "/a-long-path", url, 12));

  char name[64];
  natural_name("Austen, Jane, 1775-1817", name, sizeof(name));
  CHECK(std::string(name) == "Jane Austen");
  natural_name("Homer", name, sizeof(name));
  CHECK(std::string(name) == "Homer");
  natural_name(" Tolstoy , Leo ", name, sizeof(name));
  CHECK(std::string(name) == "Leo Tolstoy");

  char query[64];
  CHECK(url_encode_query("war & peace", query, sizeof(query)) && std::string(query) == "war+%26+peace");
  CHECK(url_encode_query("\xC5\xBE", query, sizeof(query)) && std::string(query) == "%C5%BE");
  CHECK(!url_encode_query("too long for this", query, 8));
  CHECK(store::search_url("Moby Dick", url, sizeof(url)) &&
        std::string(url) == "https://www.gutenberg.org/ebooks/search.opds/?query=Moby+Dick");

  char size[16];
  store::format_size(Language::English, 24994304, size, sizeof(size));
  CHECK(std::string(size) == "23.8 MB");
  store::format_size(Language::Lithuanian, 1572864, size, sizeof(size));
  CHECK(std::string(size) == "1,5 MB");
  store::format_size(Language::English, 397286, size, sizeof(size));
  CHECK(std::string(size) == "388 KB");
}

// Serves the test feeds and books in place of the internet.
class FakeNetwork : public Network {
 public:
  NetResult get(const char* url, BodyFn body, void* ctx) override {
    ++gets;
    last_url = url;
    joined = true;
    if (fail != NetResult::Ok) return fail;
    std::string data;
    const std::string u = url;
    const std::string dir = VELLUM_TEST_DATA "/store/";
    if (u.find("standardebooks.org/feeds") != std::string::npos) {
      data = read_all((dir + "standard-ebooks-new.atom").c_str());
    } else if (u.find("start_index=26") != std::string::npos) {
      data = read_all((dir + "gutenberg-popular-2.opds").c_str());
    } else if (u.find("search.opds") != std::string::npos) {
      data = read_all((dir + "gutenberg-popular.opds").c_str());
    } else if (u.find(".opds") != std::string::npos) {
      data = read_all((dir + "gutenberg-book.opds").c_str());
    } else if (u.find("epub") != std::string::npos) {
      data = book.empty() ? read_all(VELLUM_TEST_DATA "/sample.epub") : book;
    } else {
      return NetResult::NoServer;
    }
    // In pieces, as a network gives it; the length only when asked to say it.
    for (size_t i = 0; i < data.size(); i += 1000) {
      const uint32_t n = static_cast<uint32_t>(std::min<size_t>(1000, data.size() - i));
      if (!body(ctx, reinterpret_cast<const uint8_t*>(data.data() + i), n,
                give_length ? static_cast<uint32_t>(data.size()) : 0))
        return NetResult::Stopped;
      if (break_after > 0 && i + n >= break_after) return NetResult::BrokeOff;
    }
    return NetResult::Ok;
  }
  void disconnect() override {
    joined = false;
    ++disconnects;
  }

  int gets = 0;
  int disconnects = 0;
  bool joined = false;
  bool give_length = true;
  size_t break_after = 0;  // bytes after which the download breaks off; 0 never
  NetResult fail = NetResult::Ok;
  std::string book;  // served for EPUB links instead of the sample book
  std::string last_url;
};

// Card storage whose writes fail after `room` bytes.
class SmallCard : public StdioStorage {
 public:
  using StdioStorage::StdioStorage;
  Writer* create(const char* path) override {
    inner_ = StdioStorage::create(path);
    if (inner_ == nullptr) return nullptr;
    used_ = 0;
    return &wrap_;
  }
  bool finish(Writer*, bool keep) override { return StdioStorage::finish(inner_, keep); }
  uint32_t room = 1000;

 private:
  class Wrap : public Writer {
   public:
    explicit Wrap(SmallCard& c) : c_(c) {}
    bool write(const void* data, uint32_t len) override {
      if (c_.used_ + len > c_.room) return false;
      c_.used_ += len;
      return c_.inner_->write(data, len);
    }

   private:
    SmallCard& c_;
  };
  Writer* inner_ = nullptr;
  uint32_t used_ = 0;
  Wrap wrap_{*this};
};

void test_store_download() {
  std::filesystem::remove_all("test-store");
  std::filesystem::create_directories("test-store/books");
  StdioStorage card("test-store");
  char path[160];
  CHECK(store::book_path(card, "Frankenstein; Or, The Modern Prometheus", "Mary Wollstonecraft Shelley", path,
                         sizeof(path)));
  CHECK(std::string(path) == "books/Mary Wollstonecraft Shelley - Frankenstein.epub");
  CHECK(store::book_path(card, "What? Why: Now", "", path, sizeof(path)) &&
        std::string(path) == "books/What_ Why.epub");
  CHECK(store::book_path(card, "../../etc", "a/b", path, sizeof(path)) &&
        std::string(path).find("..") == std::string::npos && std::string(path).find('/', 6) == std::string::npos);

  FakeNetwork net;
  std::vector<int> seen;
  auto progress = [](void* ctx, int percent) { static_cast<std::vector<int>*>(ctx)->push_back(percent); };
  CHECK(store::book_path(card, "Pride and Prejudice", "Jane Austen", path, sizeof(path)));
  CHECK(store::download(net, card, "https://www.gutenberg.org/ebooks/1342.epub3.images", path, progress, &seen) ==
        store::Saved::Ok);
  CHECK(read_all("test-store/books/Jane Austen - Pride and Prejudice.epub") ==
        read_all(VELLUM_TEST_DATA "/sample.epub"));
  CHECK(!seen.empty() && seen.back() == 100);
  // Taken: the next one gets a number.
  CHECK(store::book_path(card, "Pride and Prejudice", "Jane Austen", path, sizeof(path)) &&
        std::string(path) == "books/Jane Austen - Pride and Prejudice 2.epub");

  // A web page instead of a book, a broken download and a full card leave nothing behind.
  net.book = "<!DOCTYPE html><html>Please sign in</html>";
  CHECK(store::download(net, card, "https://x.org/a.epub", path, nullptr, nullptr) == store::Saved::NotBook);
  CHECK(!std::filesystem::exists(std::string("test-store/") + path));
  net.book.clear();
  net.break_after = 2000;
  CHECK(store::download(net, card, "https://x.org/a.epub", path, nullptr, nullptr) == store::Saved::NoServer);
  CHECK(!std::filesystem::exists(std::string("test-store/") + path));
  net.break_after = 0;
  SmallCard small("test-store");
  CHECK(store::download(net, small, "https://x.org/a.epub", path, nullptr, nullptr) == store::Saved::CardFull);
  CHECK(!std::filesystem::exists(std::string("test-store/") + path));
  net.fail = NetResult::NoNetwork;
  CHECK(store::download(net, card, "https://x.org/a.epub", path, nullptr, nullptr) == store::Saved::NoNetwork);
  std::filesystem::remove_all("test-store");
}

void test_app_store() {
  std::filesystem::remove_all("test-card-store");
  std::filesystem::create_directories("test-card-store/books");
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-store");
  FakeNetwork net;
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  board.network = &net;
  auto* app = new App(board);
  app->start();

  // Home › All books; the store is the row after the four built-in books.
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);
  const int full = panel.full;
  press(*app, Key::Power);  // the store: its catalogues
  CHECK(panel.full == full + 1 && net.gets == 0);
  press(*app, Key::Power);  // Project Gutenberg: its lists, Search first
  press(*app, Key::Next);
  press(*app, Key::Power);  // Most popular
  CHECK(net.gets == 1 && net.last_url.find("sort_order=downloads") != std::string::npos);

  // The last row of the page's books is "More books"; Prev wraps to it.
  press(*app, Key::Prev);
  press(*app, Key::Power);
  CHECK(net.gets == 2 && net.last_url.find("start_index=26") != std::string::npos);
  press(*app, Key::Power);  // "Earlier books" is first, the first book is selected: open it
  CHECK(net.gets == 3 && net.last_url == "https://www.gutenberg.org/ebooks/64317.opds");
  press(*app, Key::Home);  // back to the list, on that book
  press(*app, Key::Prev);  // "Earlier books"
  press(*app, Key::Power);
  CHECK(net.gets == 4 && net.last_url.find("start_index") == std::string::npos);

  // A book: its own feed, then the download.
  press(*app, Key::Power);  // Pride and Prejudice
  CHECK(net.gets == 5 && net.last_url == "https://www.gutenberg.org/ebooks/1342.opds");
  press(*app, Key::Power);  // Download
  CHECK(net.gets == 6 && net.last_url == "https://www.gutenberg.org/ebooks/1342.epub3.images");
  CHECK(std::filesystem::exists("test-card-store/books/Jane Austen - Pride and Prejudice.epub"));
  CHECK(net.joined);
  const int full_before_reading = panel.full;
  press(*app, Key::Power);  // Read now: the radio goes off and the book opens
  CHECK(!net.joined && net.disconnects == 1);
  CHECK(panel.full == full_before_reading + 1);
  const int fast = panel.fast;
  press(*app, Key::Next);  // a page turns: it is the reader
  CHECK(panel.fast == fast + 1);

  // No home Wi-Fi: it says so; Try again asks once more; Home climbs out.
  press(*app, Key::Home);
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);  // All books
  tap(*app, 240, 645);      // "Get free books", after the five books

  press(*app, Key::Next);
  press(*app, Key::Power);  // Standard Ebooks
  net.fail = NetResult::NoNetwork;
  const int gets = net.gets;
  press(*app, Key::Power);  // New releases
  press(*app, Key::Power);  // Try again
  CHECK(net.gets == gets + 2);
  net.fail = NetResult::Ok;
  tap(*app, 240, 290);  // Try again, by touch: the list arrives
  CHECK(net.gets == gets + 3 && net.last_url.find("standardebooks.org") != std::string::npos);
  for (int i = 0; i < 3; ++i) press(*app, Key::Home);  // list, catalogues, out of the store
  CHECK(net.disconnects == 2);
  delete app;
  std::filesystem::remove_all("test-card-store");
}

// ---- Library: keyboard, search, sorting, shelves -----------------------------

// Taps the middle of a key of the keyboard.
Keyboard::Result tap_key(Keyboard& kb, const Framebuffer& fb, std::string_view label) {
  const int i = kb.find(label);
  CHECK(i >= 0);
  const Rect r = kb.key_rect(fb, i);
  return kb.tap(fb, r.x + r.w / 2, r.y + r.h / 2);
}

void test_keyboard() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer fb(buf.data(), kW, kH);
  fb.set_rotation(Rotation::R90);
  using R = Keyboard::Result;
  Keyboard kb;
  kb.open("", {.prompt = "Search", .ok = "OK", .cancel = "Cancel"});
  CHECK(kb.is_open() && kb.page() == Keyboard::Page::English && kb.text().empty());

  // Every key is finger-sized, inside the keyboard, and apart from the others.
  const Rect area = kb.area(fb);
  CHECK(area.y == fb.height() - Keyboard::kHeight && area.w == fb.width());
  for (Keyboard::Page page : {Keyboard::Page::English, Keyboard::Page::Lithuanian, Keyboard::Page::Symbols}) {
    kb.set_page(page);
    for (int i = 0; i < kb.key_count(); ++i) {
      const Rect r = kb.key_rect(fb, i);
      CHECK(r.w >= 40 && r.h >= 40);
      CHECK(r.x >= 0 && r.x + r.w <= fb.width() && r.y >= area.y && r.y + r.h <= fb.height());
      for (int j = 0; j < i; ++j) {
        const Rect o = kb.key_rect(fb, j);
        const bool apart = r.x >= o.x + o.w || o.x >= r.x + r.w || r.y >= o.y + o.h || o.y >= r.y + r.h;
        CHECK(apart);
      }
    }
  }
  kb.set_page(Keyboard::Page::English);

  // Letters, shift for one capital, and space.
  CHECK(tap_key(kb, fb, "q") == R::Typed);
  CHECK(tap_key(kb, fb, "shift") == R::Redraw && kb.shifted());
  CHECK(tap_key(kb, fb, "w") == R::Typed && !kb.shifted());
  CHECK(tap_key(kb, fb, "e") == R::Typed);
  CHECK(tap_key(kb, fb, "space") == R::Typed);
  CHECK(kb.text() == "qWe ");

  // Lithuanian letters: all nine, on the top row of their page; backspace
  // takes a whole letter.
  CHECK(tap_key(kb, fb, "language") == R::Redraw && kb.page() == Keyboard::Page::Lithuanian);
  for (const char* letter : {"ą", "č", "ę", "ė", "į", "š", "ų", "ū", "ž"}) CHECK(kb.find(letter) >= 0);
  tap_key(kb, fb, "ž");
  tap_key(kb, fb, "shift");
  tap_key(kb, fb, "ū");
  CHECK(kb.text() == "qWe žŪ");
  CHECK(tap_key(kb, fb, "back") == R::Typed && kb.text() == "qWe ž");
  kb.backspace();
  CHECK(kb.text() == "qWe ");

  // Digits and symbols on the third page.
  CHECK(tap_key(kb, fb, "symbols") == R::Redraw && kb.page() == Keyboard::Page::Symbols);
  tap_key(kb, fb, "4");
  tap_key(kb, fb, "€");
  tap_key(kb, fb, "?");
  CHECK(kb.text() == "qWe 4€?");
  CHECK(kb.find("q") < 0);
  tap_key(kb, fb, "symbols");  // ABC: back to the letters
  CHECK(kb.page() == Keyboard::Page::English && kb.find("q") >= 0);

  // OK and Cancel; a tap above the keys is not the keyboard's.
  CHECK(tap_key(kb, fb, "ok") == R::Done);
  CHECK(tap_key(kb, fb, "cancel") == R::Cancel);
  CHECK(kb.tap(fb, 200, area.y - 10) == R::None);

  // Without touch: the first press shows the cursor (on q), Power types.
  kb.open("ab", {});
  CHECK(kb.key(Key::Next) == R::Redraw);
  CHECK(kb.key(Key::Power) == R::Typed && kb.text() == "abq");
  CHECK(kb.key(Key::Next) == R::Redraw);
  CHECK(kb.key(Key::Power) == R::Typed && kb.text() == "abqw");
  CHECK(kb.key(Key::Prev) == R::Redraw && kb.key(Key::Prev) == R::Redraw);
  kb.key(Key::Power);
  CHECK(kb.text() == "abqw0");  // left of q: the last digit above it
  // Prev from q passes the digits to Cancel, then wraps to the last key.
  kb.open("", {.cancel = "Cancel"});
  kb.key(Key::Next);
  for (int i = 0; i < 11; ++i) kb.key(Key::Prev);
  CHECK(kb.cursor() == 0);
  kb.key(Key::Prev);
  CHECK(kb.cursor() == kb.key_count() - 1 && kb.key(Key::Power) == R::Done);  // OK, the last key
  kb.key(Key::Next);
  CHECK(kb.cursor() == 0 && kb.key(Key::Power) == R::Cancel);

  // Names start with a capital; the text keeps to its length.
  kb.open("", {.capitalize = true, .max_bytes = 4});
  CHECK(kb.shifted());
  tap_key(kb, fb, "v");
  CHECK(kb.text() == "V" && !kb.shifted());
  tap_key(kb, fb, "back");
  CHECK(kb.text().empty() && kb.shifted());
  CHECK(kb.type("abcd") && !kb.type("e") && kb.text() == "abcd");
  CHECK(tap_key(kb, fb, "x") == R::None);

  // It draws only in its own part of the screen.
  fb.clear(Color::White);
  fb.clear_dirty();
  kb.draw(fb);
  const Rect dirty = fb.dirty();  // physical: the panel's left part holds the bottom of the portrait
  CHECK(dirty.w == Keyboard::kHeight && dirty.h == kH);
}

void test_search_folding() {
  char out[64];
  CHECK(fold_for_search("Žemaitė", out, sizeof(out)) == 7 && std::string(out) == "zemaite");
  fold_for_search("ĄČĘĖĮŠŲŪŽ ąčęėįšųūž", out, sizeof(out));
  CHECK(std::string(out) == "aceeisuuz aceeisuuz");
  fold_for_search("Crème Brûlée, Ærø ÷", out, sizeof(out));
  CHECK(std::string(out) == "creme brulee, aro ÷");                          // signs keep their own
  CHECK(fold_for_search("Žžžž", out, 4) == 3 && std::string(out) == "zzz");  // cut to fit
  CHECK(matches_search("Žemaitė", "zemaite"));
  CHECK(matches_search("Žemaitė", "ŽEM"));
  CHECK(matches_search("Antanas Baranauskas", "  BARAN "));
  CHECK(matches_search("Anykščių šilelis", "anyksciu"));
  CHECK(matches_search("anything", ""));
  CHECK(!matches_search("Žemaitė", "zemaites"));
  CHECK(!matches_search("Marti", "zem"));
}

void test_library_sorting() {
  Library lib;
  Epub epub;
  lib.load(builtin_books(), nullptr, epub);
  // Library order: Alice, Moby-Dick, Persuasion, The Wisdom of Father Brown.
  int order[Library::kMaxBooks];
  CHECK(lib.sorted(LibrarySort::Title, order, Library::kMaxBooks) == 4);
  CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3);
  // By surname: Austen, Carroll, Chesterton, Melville.
  CHECK(lib.sorted(LibrarySort::Author, order, Library::kMaxBooks) == 4);
  CHECK(order[0] == 2 && order[1] == 0 && order[2] == 3 && order[3] == 1);
  lib.touch(3);
  lib.touch(1);
  CHECK(lib.sorted(LibrarySort::Recent, order, Library::kMaxBooks) == 4);
  CHECK(order[0] == 1 && order[1] == 3 && order[2] == 0 && order[3] == 2);
  CHECK(lib.sorted(LibrarySort::Author, order, 2) == 2 && order[0] == 2 && order[1] == 0);

  // Finished, and Reading now (started, not finished).
  BookState s;
  CHECK(!s.started() && !s.reading());
  s.last_opened = 5;
  CHECK(s.reading());
  s.finished = true;
  CHECK(!s.reading());
}

void test_shelves_store() {
  StdioStorage card("test-card-shelves");
  std::remove("test-card-shelves/vellum/shelves.txt");
  Shelves shelves;
  shelves.load(card);
  CHECK(shelves.count() == 0);
  CHECK(shelves.add("  Poezija \t") == 0 && std::string(shelves.name(0)) == "Poezija");
  CHECK(shelves.add("   ") == -1);  // a name is needed
  CHECK(shelves.add("Vasara") == 1);
  CHECK(shelves.add("Klasika") == 2);
  CHECK(shelves.set(0, "books/maironis.epub", true));
  CHECK(shelves.set(1, "books/maironis.epub", true));
  CHECK(shelves.set(1, "builtin:Persuasion", true));
  CHECK(shelves.set(1, "builtin:Persuasion", true));  // once only
  CHECK(shelves.set(2, "builtin:Moby-Dick", true));
  CHECK(shelves.contains(0, "books/maironis.epub") && !shelves.contains(0, "builtin:Persuasion"));
  CHECK(shelves.set(1, "books/maironis.epub", false) && !shelves.contains(1, "books/maironis.epub"));
  CHECK(shelves.rename(1, "Vasarai") && !shelves.rename(1, "") && std::string(shelves.name(1)) == "Vasarai");
  CHECK(!shelves.set(7, "x", true));
  CHECK(shelves.find(" klasika ") == 2 && shelves.find("Klasikos") == -1);
  CHECK(shelves.save(card));
  CHECK(read_all("test-card-shelves/vellum/shelves.txt") ==
        "Poezija\n\tbooks/maironis.epub\nVasarai\n\tbuiltin:Persuasion\nKlasika\n\tbuiltin:Moby-Dick\n");

  Shelves again;
  again.load(card);
  CHECK(again.count() == 3 && std::string(again.name(2)) == "Klasika");
  CHECK(again.contains(0, "books/maironis.epub") && again.contains(1, "builtin:Persuasion") &&
        again.contains(2, "builtin:Moby-Dick"));
  // Deleting a shelf takes its books off it; later shelves move up.
  again.remove(1);
  CHECK(again.count() == 2 && std::string(again.name(1)) == "Klasika" && again.contains(1, "builtin:Moby-Dick"));
  CHECK(!again.contains(0, "builtin:Persuasion") && again.contains(0, "books/maironis.epub"));
  Library lib;
  Epub epub;
  lib.load(builtin_books(), nullptr, epub);
  CHECK(again.books_on(1, lib) == 1 && again.books_on(0, lib) == 0);  // Maironis is not on this card
  for (int i = again.count(); i < Shelves::kMax; ++i) CHECK(again.add("More") >= 0);
  CHECK(again.add("One too many") == -1);
  std::remove("test-card-shelves/vellum/shelves.txt");
}

void test_app_library() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer screen(buf.data(), kW, kH);
  screen.set_rotation(Rotation::R90);
  CountingPanel panel;
  NoInput input;
  SleepyPower power;
  StdioStorage card("test-card-shelves");
  for (const char* name : {"state.txt", "progress.txt", "settings.txt", "shelves.txt", "library.txt"}) {
    std::remove(("test-card-shelves/vellum/" + std::string(name)).c_str());
  }
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  CHECK(card.write("books/sample.epub", book.data(), static_cast<uint32_t>(book.size())));
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  const auto file = [](const char* name) {
    return read_all(("test-card-shelves/vellum/" + std::string(name)).c_str());
  };
  // The keyboard as the app draws it, for finding keys.
  Keyboard kb;
  kb.open("", {.cancel = "Cancel"});
  const auto type = [&](std::string_view letters) {
    size_t i = 0;
    while (i < letters.size()) {
      const size_t begin = i;
      decode_utf8(letters, i);
      const Rect r = kb.key_rect(screen, kb.find(letters.substr(begin, i - begin)));
      tap(*app, r.x + r.w / 2, r.y + r.h / 2);
    }
  };
  const auto tap_named = [&](const char* name) {
    const Rect r = kb.key_rect(screen, kb.find(name));
    tap(*app, r.x + r.w / 2, r.y + r.h / 2);
  };
  // Home: the current book, two others, then All books.
  const auto open_library = [&]() {
    press(*app, Key::Home);
    for (int i = 0; i < 3; ++i) press(*app, Key::Next);
    press(*app, Key::Power);
  };

  // Items: Sort, Search, the tabs All, Reading now, Finished, New shelf;
  // then the books, the first one selected.
  open_library();
  for (int i = 0; i < 6; ++i) press(*app, Key::Prev);
  press(*app, Key::Power);  // Sort: Title, kept in Settings
  CHECK(file("settings.txt").find("library_sort=1") != std::string::npos);
  press(*app, Key::Power);  // Author
  CHECK(file("settings.txt").find("library_sort=2") != std::string::npos);

  // Search with the keyboard: "arunas" finds the book by Arūnas Tester.
  press(*app, Key::Next);
  press(*app, Key::Power);
  const int refreshes = panel.fast + panel.full;
  type("arunas");
  CHECK(panel.fast + panel.full == refreshes + 6);  // the list follows each letter
  tap_named("ok");
  press(*app, Key::Power);  // the one book found, selected
  CHECK(file("progress.txt").find("books/sample.epub\t") != std::string::npos);

  // A new shelf from the last tab; its name starts with a capital.
  open_library();
  press(*app, Key::Prev);  // the New shelf tab
  press(*app, Key::Power);
  type("poezija");
  tap_named("ok");
  CHECK(file("shelves.txt") == "Poezija\n");

  // Holding a book in All opens its shelves: put it on Poezija, mark it finished.
  for (int i = 0; i < 3; ++i) press(*app, Key::Prev);  // the shelf's tab was selected: back to All
  press(*app, Key::Power);
  hold(*app, 240, 268);  // the first book by author: Austen's Persuasion
  press(*app, Key::Next);
  press(*app, Key::Next);  // the label is skipped: Poezija
  press(*app, Key::Power);
  CHECK(file("shelves.txt") == "Poezija\n\tbuiltin:Persuasion\n");
  press(*app, Key::Prev);
  press(*app, Key::Power);  // Finished reading
  CHECK(file("progress.txt").find("builtin:Persuasion\t0\t0\t0\t0\t0\t1\n") != std::string::npos);
  // A new shelf from here takes the book too.
  tap(*app, 240, 160 + 4 * 50 + 25);
  type("vasara");
  tap_named("ok");
  CHECK(file("shelves.txt") == "Poezija\n\tbuiltin:Persuasion\nVasara\n\tbuiltin:Persuasion\n");
  press(*app, Key::Prev);
  press(*app, Key::Prev);
  press(*app, Key::Prev);
  press(*app, Key::Power);  // Done: back to the library

  // Rename Poezija with Lithuanian letters, then delete it. Items: Sort,
  // Search, All, Reading now, Finished, Poezija, Vasara, New shelf, the books.
  open_library();
  for (int i = 0; i < 3; ++i) press(*app, Key::Prev);  // from the first book to Poezija
  press(*app, Key::Power);                             // Rename and Delete come after the tabs
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);  // Rename: the keyboard holds the name
  for (int i = 0; i < 7; ++i) tap_named("back");
  type("e");
  type("il");
  tap_named("language");
  kb.set_page(Keyboard::Page::Lithuanian);
  type("ė");
  type("s");
  tap_named("ok");
  kb.set_page(Keyboard::Page::English);
  CHECK(file("shelves.txt").starts_with("Eilės\n\tbuiltin:Persuasion\nVasara\n"));
  press(*app, Key::Next);
  press(*app, Key::Power);  // Delete asks again
  CHECK(file("shelves.txt").starts_with("Eilės\n"));
  press(*app, Key::Power);
  CHECK(file("shelves.txt") == "Vasara\n\tbuiltin:Persuasion\n");

  // Cancel puts the search back as it was: nothing.
  open_library();
  for (int i = 0; i < 6; ++i) press(*app, Key::Prev);  // Search
  press(*app, Key::Power);
  type("zz");
  tap_named("cancel");
  hold(*app, 240, 268);  // a book again, so its shelves open: take it off Vasara
  press(*app, Key::Next);
  press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(file("shelves.txt") == "Vasara\n");

  // Reading to the last page marks a book finished.
  open_library();
  for (int i = 0; i < 7; ++i) press(*app, Key::Prev);
  press(*app, Key::Power);  // Sort: Added
  CHECK(file("settings.txt").find("library_sort=3") != std::string::npos);
  press(*app, Key::Power);  // back to Recent; the sample book first
  for (int i = 0; i < 7; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  const auto sample_line = [&]() {
    const std::string all = file("progress.txt");
    const size_t at = all.find("books/sample.epub\t");
    return at == std::string::npos ? std::string() : all.substr(at, all.find('\n', at) - at);
  };
  CHECK(sample_line().ends_with("\t0"));
  for (int i = 0; i < 40; ++i) press(*app, Key::Next);
  CHECK(sample_line().starts_with("books/sample.epub\t1\t") && sample_line().ends_with("\t1"));
  delete app;
  for (const char* name : {"state.txt", "progress.txt", "settings.txt", "shelves.txt", "library.txt"}) {
    std::remove(("test-card-shelves/vellum/" + std::string(name)).c_str());
  }
}

void test_app_store_search() {
  std::filesystem::remove_all("test-card-store-search");
  std::filesystem::create_directories("test-card-store-search/books");
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer screen(buf.data(), kW, kH);
  screen.set_rotation(Rotation::R90);
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-store-search");
  FakeNetwork net;
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  board.network = &net;
  auto* app = new App(board);
  app->start();
  Keyboard kb;
  kb.open("", {.cancel = "Cancel"});
  const auto type = [&](std::string_view letters) {
    for (char c : letters) {
      const Rect r = kb.key_rect(screen, kb.find(c == ' ' ? std::string_view("space") : std::string_view(&c, 1)));
      tap(*app, r.x + r.w / 2, r.y + r.h / 2);
    }
  };
  const auto tap_named = [&](const char* name) {
    const Rect r = kb.key_rect(screen, kb.find(name));
    tap(*app, r.x + r.w / 2, r.y + r.h / 2);
  };

  // Library › Get free books › Project Gutenberg › Search: the keyboard.
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);  // past the four built-in books
  press(*app, Key::Power);
  press(*app, Key::Power);
  press(*app, Key::Power);
  CHECK(net.gets == 0);
  type("moby dick");
  CHECK(net.gets == 0);  // nothing is fetched while typing
  tap_named("ok");
  CHECK(net.gets == 1 && net.last_url == "https://www.gutenberg.org/ebooks/search.opds/?query=moby+dick");
  press(*app, Key::Power);  // the first book found opens
  CHECK(net.gets == 2 && net.last_url.find(".opds") != std::string::npos);

  // Cancel, or OK with nothing typed, fetches nothing.
  press(*app, Key::Home);  // the results
  press(*app, Key::Home);  // the lists, on Search
  press(*app, Key::Power);
  tap_named("ok");
  CHECK(net.gets == 2);
  press(*app, Key::Power);
  type("x");
  press(*app, Key::Home);
  CHECK(net.gets == 2);
  for (int i = 0; i < 2; ++i) press(*app, Key::Home);  // the catalogues, out of the store

  // A library search that finds nothing offers the store's search for the same words.
  for (int i = 0; i < 9; ++i) press(*app, Key::Prev);  // from Get free books, past the books and tabs, to Search
  press(*app, Key::Power);
  type("zzz");
  tap_named("ok");  // nothing found: Search stays selected
  press(*app, Key::Prev);
  press(*app, Key::Prev);  // wraps to the last row: "Search free books for “zzz”"
  press(*app, Key::Power);
  CHECK(net.gets == 3 && net.last_url == "https://www.gutenberg.org/ebooks/search.opds/?query=zzz");
  delete app;
  std::filesystem::remove_all("test-card-store-search");
}

// ---- Buttons, screen orientation, auto page turn, refresh cadence, screenshots ----

void test_button_sets() {
  using A = ButtonAction;
  CHECK(buttons_safe(kDefaultButtons.reader, true));
  CHECK(buttons_safe(kDefaultButtons.menu, false));
  CHECK(!buttons_safe(kDefaultButtons.reader, false));  // page turns are not menu actions

  // While reading, something must lead out: the contents, home or the library.
  ButtonAction reader[kButtonInputs] = {A::PrevPage, A::NextPage, A::Contents, A::None, A::None, A::None, A::None};
  CHECK(buttons_safe(reader, true));
  CHECK(next_button_action(reader, true, 2) == A::Home);  // the next one that still leads out
  reader[2] = A::Library;
  CHECK(next_button_action(reader, true, 2) == A::Contents);  // past all that would not, around to Contents
  reader[3] = A::Home;
  CHECK(next_button_action(reader, true, 2) == A::Bookmark);  // Home leads out now
  reader[3] = A::None;
  reader[2] = A::Bookmark;
  CHECK(!buttons_safe(reader, true));

  // In menus: something selects, something goes back, something moves.
  ButtonAction menu[kButtonInputs];
  std::copy(std::begin(kDefaultButtons.menu), std::end(kDefaultButtons.menu), menu);
  CHECK(next_button_action(menu, false, 2) == A::Select);  // Power is the only Select: it stays
  CHECK(next_button_action(menu, false, 3) == A::Back);    // and Home the only Back
  CHECK(next_button_action(menu, false, 0) == A::Down);    // Prev may leave Up: Next still moves
  menu[5] = A::Select;                                     // Next held selects too
  CHECK(next_button_action(menu, false, 2) == A::Back);
  menu[0] = A::None;
  menu[1] = A::PageDown;
  CHECK(!buttons_safe(menu, false));  // paging alone does not move the highlight

  CHECK(button_input(Key::Prev, false) == static_cast<int>(ButtonInput::Prev));
  CHECK(button_input(Key::Home, true) == static_cast<int>(ButtonInput::HomeHeld));
  CHECK(button_input(Key::Power, false) == static_cast<int>(ButtonInput::Power));
  CHECK(button_input(Key::Power, true) == -1);  // holding Power always sleeps

  // Saved as key=value lines; a set that is unsafe, not offered or cut short is ignored.
  std::filesystem::remove_all("test-card-buttons");
  StdioStorage card("test-card-buttons");
  Settings s;
  s.buttons.reader[4] = A::PrevChapter;
  s.buttons.reader[5] = A::NextChapter;
  s.buttons.menu[6] = A::Screenshot;
  s.orientation = Orientation::LandscapeRight;
  s.auto_turn = 0;
  s.full_refresh = FullRefresh::NewChapter;
  CHECK(save_settings(card, s));
  const std::string saved = read_all("test-card-buttons/vellum/settings.txt");
  CHECK(saved.find("buttons_reading=1,2,5,6,3,4,0\n") != std::string::npos);
  CHECK(saved.find("buttons_menus=15,16,17,18,0,0,13\n") != std::string::npos);
  Settings back;
  load_settings(card, back);
  CHECK(back == s);
  for (const char* bad : {"buttons_reading=1,2,8,0,0,0,0\n",      // nothing leads out
                          "buttons_menus=1,2,17,18,0,0,0\n",      // page turns in menus
                          "buttons_menus=15,16,17\n",             // cut short
                          "buttons_reading=1,2,5,6,3,4,0,0\n",    // one too many
                          "buttons_reading=1,2,5,6,3,4,99\n"}) {  // no such action
    card.write("vellum/settings.txt", bad, static_cast<uint32_t>(std::strlen(bad)));
    Settings loaded;
    load_settings(card, loaded);
    CHECK(loaded.buttons == kDefaultButtons);
  }
  std::filesystem::remove_all("test-card-buttons");
}

// A touch at logical (x, y) on a screen turned by r.
InputEvent touch_turned(InputEvent::Type type, Rotation r, int x, int y) {
  InputEvent ev;
  ev.type = type;
  int px, py;
  rotate_to_physical(r, kW, kH, x, y, px, py);
  ev.x = static_cast<int16_t>(px);
  ev.y = static_cast<int16_t>(py);
  return ev;
}

void tap_turned(App& app, Rotation r, int x, int y) {
  app.handle(touch_turned(InputEvent::Type::TouchDown, r, x, y));
  app.handle(touch_turned(InputEvent::Type::TouchUp, r, x, y));
}

// A card for the controls tests: the sample book, read last, and these settings.
void controls_card(StdioStorage& card, const char* settings) {
  std::filesystem::remove_all("test-card-controls");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  CHECK(card.write("books/sample.epub", book.data(), static_cast<uint32_t>(book.size())));
  const char* progress = "books/sample.epub\t0\t0\t0\t0\t1\n";
  CHECK(card.write("vellum/progress.txt", progress, static_cast<uint32_t>(std::strlen(progress))));
  CHECK(card.write("vellum/settings.txt", settings, static_cast<uint32_t>(std::strlen(settings))));
}

bool controls_progress_has(const char* fields) {
  return read_all("test-card-controls/vellum/progress.txt").find(fields) != std::string::npos;
}

void hold(App& app, TimedInput& input, Key key) {
  app.handle(key_event(InputEvent::Type::KeyDown, key));
  CHECK(app.wait_ms() == kHoldKeyMs);
  input.ms += kHoldKeyMs;
  app.timeout();
  app.handle(key_event(InputEvent::Type::KeyUp, key));
}

void test_app_buttons() {
  using T = InputEvent::Type;
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  TimedInput input;
  SleepyPower power;
  StdioStorage card("test-card-controls");
  // Held Prev and Next skip chapters; held Home saves a screenshot.
  controls_card(card, "buttons_reading=1,2,5,6,3,4,13\n");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);  // open the book: Power, in menus, still selects
  CHECK(controls_progress_has("books/sample.epub\t0\t0\t0\t"));

  // A button with a held action acts on release when let go at once.
  const int fast = panel.fast;
  app->handle(key_event(T::KeyDown, Key::Next));
  CHECK(panel.fast == fast);  // not yet: it may be held
  CHECK(app->wait_ms() == kHoldKeyMs);
  input.ms += 100;
  CHECK(app->wait_ms() == kHoldKeyMs - 100);
  app->handle(key_event(T::KeyUp, Key::Next));
  CHECK(panel.fast == fast + 1);  // the next page
  press(*app, Key::Prev);
  CHECK(panel.fast == fast + 2);

  // Held: the next chapter, and back to the one before from its first page.
  int full = panel.full;
  hold(*app, input, Key::Next);
  CHECK(panel.full == full + 1);
  press(*app, Key::Power);  // contents ...
  press(*app, Key::Power);  // ... and back, which saves the place
  CHECK(controls_progress_has("books/sample.epub\t1\t0\t0\t"));
  hold(*app, input, Key::Prev);
  press(*app, Key::Power);
  press(*app, Key::Power);
  CHECK(controls_progress_has("books/sample.epub\t0\t0\t0\t"));

  // A held press without a timeout (a board that kept sending events) counts on release.
  full = panel.full;
  app->handle(key_event(T::KeyDown, Key::Next));
  input.ms += kHoldKeyMs + 50;
  app->handle(key_event(T::KeyUp, Key::Next));
  CHECK(panel.full == full + 1);

  // Held Home: a screenshot, and a notice that goes away by itself.
  hold(*app, input, Key::Home);
  CHECK(read_all("test-card-controls/screenshots/screenshot-0001.bmp").size() == screenshot::bmp_size(480, 800));
  CHECK(app->wait_ms() == 2500);  // the notice
  const int fast_now = panel.fast;
  input.ms += 2500;
  app->timeout();
  CHECK(panel.fast == fast_now + 1 && power.sleeps == 0);  // gone, and still awake
  CHECK(app->wait_ms() == 10 * 60 * 1000);

  // Holding Power still sleeps, whatever the buttons do.
  app->handle(key_event(T::KeyDown, Key::Power));
  app->timeout();
  CHECK(power.sleeps == 1);
  app->handle(key_event(T::KeyUp, Key::Power));
  press(*app, Key::Next);  // wake
  press(*app, Key::Home);  // a short Home: home, as always

  // Settings › Buttons: the last but one row of the general page.
  press(*app, Key::Prev);  // Settings, the last link
  press(*app, Key::Power);
  press(*app, Key::Prev);
  press(*app, Key::Prev);
  full = panel.full;
  press(*app, Key::Power);
  CHECK(panel.full == full + 1);
  auto saved = [] { return read_all("test-card-controls/vellum/settings.txt"); };
  // Power while reading: Contents, Home screen, Library, then around to Contents
  // (Home does nothing while reading here, so nothing else would lead out).
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(saved().find("buttons_reading=1,2,6,6,3,4,13\n") != std::string::npos);
  press(*app, Key::Next);  // Home: away from Home screen, as Power leads out now
  press(*app, Key::Power);
  CHECK(saved().find("buttons_reading=1,2,6,7,3,4,13\n") != std::string::npos);
  for (int i = 0; i < 14; ++i) press(*app, Key::Power);  // all the way around, to Home screen again
  CHECK(saved().find("buttons_reading=1,2,6,6,3,4,13\n") != std::string::npos);

  // In menus, Power is the only Select: it stays.
  for (int i = 0; i < 4; ++i) press(*app, Key::Prev);  // the tabs
  press(*app, Key::Power);                             // In menus
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(saved().find("buttons_menus=15,16,17,18,0,0,0\n") != std::string::npos);
  // Reset to default, the last row.
  press(*app, Key::Prev);
  press(*app, Key::Prev);
  press(*app, Key::Prev);
  press(*app, Key::Prev);  // the tabs, then around to Reset
  press(*app, Key::Power);
  CHECK(saved().find("buttons_reading=1,2,5,6,0,0,0\n") != std::string::npos);
  // Home goes back to Settings, on the Buttons row.
  full = panel.full;
  press(*app, Key::Home);
  CHECK(panel.full == full + 1);
  press(*app, Key::Power);
  CHECK(panel.full == full + 2);  // the buttons screen again
  delete app;

  // Remapped in menus: Prev opens, Next goes down, Power goes back, Home goes up.
  controls_card(card, "buttons_menus=17,16,18,15,0,0,0\n");
  app = new App(board);
  app->start();
  full = panel.full;
  press(*app, Key::Prev);  // opens the current book
  CHECK(panel.full == full + 1);
  press(*app, Key::Power);  // reading: Power still opens the contents
  CHECK(panel.full == full + 2);
  press(*app, Key::Power);  // in the contents, Power goes back: home
  CHECK(panel.full == full + 3);
  delete app;
  std::filesystem::remove_all("test-card-controls");
}

bool any_black(const Framebuffer& fb, Rect r) {
  for (int y = r.y; y < r.y + r.h; ++y)
    for (int x = r.x; x < r.x + r.w; ++x)
      if (fb.pixel(x, y) == Color::Black) return true;
  return false;
}

void test_app_orientation() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-controls");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};

  // Upside down: every screen turns, touches with it, and Prev and Next trade places.
  controls_card(card, "orientation=1\n");
  auto* app = new App(board);
  app->start();
  Framebuffer upside(buf.data(), kW, kH);
  upside.set_rotation(Rotation::R270);
  CHECK(any_black(upside, {380, 20, 72, 30}));  // the battery, top right as the reader holds it
  tap_turned(*app, Rotation::R270, 240, 200);   // the current book's card
  CHECK(panel.full == 2);
  const int fast = panel.fast;
  press(*app, Key::Prev);  // towards the reading: the next page
  CHECK(panel.fast == fast + 1);
  press(*app, Key::Next);
  CHECK(panel.fast == fast + 2);  // and Next back again
  delete app;

  // Landscape: the reader lies on its side at 800x480; menus stay upright.
  controls_card(card, "orientation=2\n");
  app = new App(board);
  app->start();
  Framebuffer portrait(buf.data(), kW, kH);
  portrait.set_rotation(Rotation::R90);
  CHECK(any_black(portrait, {380, 20, 72, 30}));  // home: the battery where it always is
  press(*app, Key::Power);                        // open the book
  Framebuffer wide(buf.data(), kW, kH);
  wide.set_rotation(Rotation::R0);
  CHECK(any_black(wide, {700, 440, 100, 40}));  // the battery, bottom right of the wide page
  CHECK(any_black(wide, {28, 30, 744, 200}));   // the chapter's heading and text
  CHECK(!any_black(wide, {0, 0, 16, 480}));     // and stays inside the margins
  CHECK(!any_black(wide, {800 - 16, 0, 16, 480}));
  int fast_now = panel.fast;
  tap_turned(*app, Rotation::R0, 700, 240);  // the right third: forward
  CHECK(panel.fast == fast_now + 1);
  tap_turned(*app, Rotation::R0, 100, 240);  // the left third: back
  CHECK(panel.fast == fast_now + 2);
  // Contents is upright: its rows take taps in portrait.
  tap_turned(*app, Rotation::R0, 400, 240);  // the middle: contents
  CHECK(any_black(portrait, {380, 20, 72, 30}));
  const int full = panel.full;
  tap_turned(*app, Rotation::R90, 240, 186 + 25);  // Back to book
  CHECK(panel.full == full + 1);
  CHECK(any_black(wide, {700, 440, 100, 40}));  // on its side again
  delete app;

  // On its side the other way, Prev and Next trade places too.
  controls_card(card, "orientation=3\n");
  app = new App(board);
  app->start();
  press(*app, Key::Power);
  fast_now = panel.fast;
  press(*app, Key::Next);  // back: nothing before the first page
  CHECK(panel.fast == fast_now);
  press(*app, Key::Prev);
  CHECK(panel.fast == fast_now + 1);
  delete app;
  std::filesystem::remove_all("test-card-controls");
}

void test_app_auto_turn() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  TimedInput input;
  SleepyPower power;
  StdioStorage card("test-card-controls");
  controls_card(card, "auto_turn=0\n");                               // every 5 s
  std::filesystem::remove("test-card-controls/vellum/progress.txt");  // Alice, built in: many pages
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);  // open the book
  press(*app, Key::Power);  // contents: back, bookmark, light, look up, turn pages
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(app->wait_ms() == 2500);  // the notice goes first
  int fast = panel.fast;
  input.ms += 2500;
  app->timeout();
  CHECK(panel.fast == fast + 1);  // the notice went; the page stays
  CHECK(app->wait_ms() == 2500);  // the rest of the 5 s
  input.ms += 2500;
  app->timeout();
  CHECK(panel.fast == fast + 2);  // a page turned
  CHECK(app->wait_ms() == 5000);
  for (int i = 0; i < 3; ++i) {
    input.ms += 5000;
    app->timeout();
  }
  CHECK(panel.fast == fast + 5 && power.sleeps == 0);  // turning counts as use

  // Any button stops it, and does nothing else.
  app->handle(key_event(InputEvent::Type::KeyDown, Key::Next));
  app->handle(key_event(InputEvent::Type::KeyUp, Key::Next));
  CHECK(panel.fast == fast + 6);  // the "stopped" notice only
  input.ms += 2500;
  app->timeout();
  CHECK(app->wait_ms() == 10 * 60 * 1000);  // the idle time runs again

  // A touch stops it too, and is not a tap.
  press(*app, Key::Power);
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  fast = panel.fast;
  tap(*app, 400, 400);  // the right third would turn the page
  CHECK(panel.fast == fast + 1);
  tap(*app, 400, 400);  // now it does
  CHECK(panel.fast == fast + 2);
  delete app;

  // It stops at the end of the book; then the reader may sleep again.
  controls_card(card, "auto_turn=0\n");
  app = new App(board);
  app->start();
  press(*app, Key::Power);
  press(*app, Key::Power);
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  for (int i = 0; i < 20 && app->wait_ms() <= 5000; ++i) {
    input.ms += app->wait_ms();
    app->timeout();
  }
  CHECK(app->wait_ms() == 10 * 60 * 1000);
  CHECK(power.sleeps == 0);
  press(*app, Key::Home);  // saves the place: the last chapter
  CHECK(controls_progress_has("books/sample.epub\t1\t"));
  delete app;
  std::filesystem::remove_all("test-card-controls");
}

void test_app_refresh_cadence() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-controls");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  // Pages of Alice, built in, after this many: full and fast refreshes.
  auto turns = [&](const char* settings, int pages, int& full, int& fast) {
    controls_card(card, settings);
    std::filesystem::remove("test-card-controls/vellum/progress.txt");
    auto* app = new App(board);
    app->start();
    press(*app, Key::Power);
    full = panel.full;
    fast = panel.fast;
    for (int i = 0; i < pages; ++i) press(*app, Key::Next);
    full = panel.full - full;
    fast = panel.fast - fast;
    delete app;
  };
  int full, fast;
  turns("", 13, full, fast);  // as always: a full refresh after 12 fast ones
  CHECK(full == 1 && fast == 12);
  turns("full_refresh=1\n", 12, full, fast);  // after 5
  CHECK(full == 2 && fast == 10);
  turns("full_refresh=0\n", 3, full, fast);  // every page
  CHECK(full == 3 && fast == 0);
  turns("full_refresh=3\n", 21, full, fast);  // after 20
  CHECK(full == 1 && fast == 20);
  turns("full_refresh=4\n", 20, full, fast);  // only at a new chapter: Alice has one
  CHECK(full == 0 && fast == 20);

  // At a new chapter, in the sample book.
  controls_card(card, "full_refresh=4\n");
  auto* app = new App(board);
  app->start();
  press(*app, Key::Power);
  full = panel.full;
  press(*app, Key::Next);  // chapter one is a page long
  CHECK(panel.full == full + 1);
  delete app;
  std::filesystem::remove_all("test-card-controls");
}

void test_screenshot_bmp() {
  std::filesystem::remove_all("test-card-shots");
  StdioStorage card("test-card-shots");
  char path[64];
  screenshot::next_path(card, path, sizeof(path));
  CHECK(std::string(path) == "screenshots/screenshot-0001.bmp");
  card.write("screenshots/screenshot-0007.bmp", "x", 1);
  card.write("screenshots/notes.txt", "x", 1);
  screenshot::next_path(card, path, sizeof(path));
  CHECK(std::string(path) == "screenshots/screenshot-0008.bmp");

  // Portrait: 480 x 800, the logical top-left pixel black.
  Fixture f;
  f.fb.set_rotation(Rotation::R90);
  f.fb.clear(Color::White);
  f.fb.set_pixel(0, 0, Color::Black);
  f.fb.set_pixel(479, 799, Color::Black);
  CHECK(screenshot::save_bmp(card, f.fb, path));
  const std::string bmp = read_all("test-card-shots/screenshots/screenshot-0008.bmp");
  CHECK(bmp.size() == 62 + 60 * 800 && bmp.size() == screenshot::bmp_size(480, 800));
  auto u32 = [&](size_t at) {
    return uint32_t{uint8_t(bmp[at])} | uint32_t{uint8_t(bmp[at + 1])} << 8 | uint32_t{uint8_t(bmp[at + 2])} << 16 |
           uint32_t{uint8_t(bmp[at + 3])} << 24;
  };
  CHECK(bmp.size() > 62 && bmp[0] == 'B' && bmp[1] == 'M');
  CHECK(u32(2) == bmp.size() && u32(10) == 62);             // file size, pixels' offset
  CHECK(u32(18) == 480 && u32(22) == 800);                  // width, height (rows bottom up)
  CHECK(uint8_t(bmp[28]) == 1 && u32(46) == 2);             // 1 bit, two colours
  CHECK(u32(54) == 0 && (u32(58) & 0xFFFFFF) == 0xFFFFFF);  // black, white
  const size_t top_row = 62 + 799 * 60;                     // the last row stored is the top one
  CHECK(uint8_t(bmp[top_row]) == 0x7F);                     // its first pixel black
  CHECK(uint8_t(bmp[62 + 59]) == 0xFE);                     // the bottom row's last pixel black
  CHECK(uint8_t(bmp[62 + 60]) == 0xFF);

  // Landscape: 800 x 480.
  f.fb.set_rotation(Rotation::R0);
  CHECK(screenshot::save_bmp(card, f.fb, "screenshots/wide.bmp"));
  const std::string wide = read_all("test-card-shots/screenshots/wide.bmp");
  CHECK(wide.size() == screenshot::bmp_size(800, 480) && wide.size() == 62 + 100 * 480);
  std::filesystem::remove_all("test-card-shots");
}

// ---- Sleep screens -----------------------------------------------------------

// A w x h BMP of `bpp` bits whose pixel (x, y) has the grey grey_at(x, y)
// (on the palette for 1, 4 and 8 bits). `fields`: 32 bits with bit fields,
// red and blue swapped from the usual order.
int bmp_levels(int bpp) { return bpp <= 8 ? 1 << bpp : 256; }
int bmp_index(int bpp, int x, int y) { return (x * 3 + y * 5) % bmp_levels(bpp); }
int bmp_grey(int bpp, int x, int y) {
  const int n = bmp_levels(bpp);
  return bpp <= 8 ? bmp_index(bpp, x, y) * 255 / (n - 1) : (x * 20 + y * 7) % 256;
}

std::vector<uint8_t> make_bmp(int bpp, int w, int h, bool top_down, bool fields = false) {
  const int colours = bpp <= 8 ? 1 << bpp : 0;
  const uint32_t row = static_cast<uint32_t>((w * bpp + 31) / 32 * 4);
  const uint32_t data_at = 14 + 40 + (fields ? 12 : 0) + colours * 4;
  std::vector<uint8_t> out(data_at + row * h, 0);
  auto put32 = [&](size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(v >> (8 * i));
  };
  out[0] = 'B';
  out[1] = 'M';
  put32(2, static_cast<uint32_t>(out.size()));
  put32(10, data_at);
  put32(14, 40);
  put32(18, static_cast<uint32_t>(w));
  put32(22, static_cast<uint32_t>(top_down ? -h : h));
  out[26] = 1;
  out[28] = static_cast<uint8_t>(bpp);
  put32(30, fields ? 3 : 0);
  put32(46, static_cast<uint32_t>(colours));
  if (fields) {
    put32(54, 0x000000FF);  // red in the low byte
    put32(58, 0x0000FF00);
    put32(62, 0x00FF0000);
  }
  for (int i = 0; i < colours; ++i) {
    const auto v = static_cast<uint8_t>(i * 255 / (colours - 1));
    for (int c = 0; c < 3; ++c) out[14 + 40 + i * 4 + c] = v;
  }
  for (int y = 0; y < h; ++y) {
    uint8_t* r = out.data() + data_at + row * (top_down ? y : h - 1 - y);
    for (int x = 0; x < w; ++x) {
      if (bpp <= 8) {
        const int per = 8 / bpp;
        r[x / per] |= static_cast<uint8_t>(bmp_index(bpp, x, y) << (8 - bpp * (x % per + 1)));
      } else {
        const auto v = static_cast<uint8_t>(bmp_grey(bpp, x, y));
        for (int c = 0; c < 3; ++c) r[x * (bpp / 8) + c] = v;
      }
    }
  }
  return out;
}

// ---- Library views: folders, recently added, moving finished books, the
// cover grid, the folder browser and pictures.

void write_file(StdioStorage& card, const char* path, const std::string& data) {
  CHECK(card.write(path, data.data(), static_cast<uint32_t>(data.size())));
}

// A Windows bitmap of `bits` 1, 8 or 24 a pixel, rows stored bottom to top
// unless top_down: black on the top half, white below.
std::string make_half_bmp(int w, int h, int bits, bool top_down) {
  const int stride = ((w * bits + 31) / 32) * 4;
  const int colours = bits <= 8 ? 1 << bits : 0;
  const int offset = 14 + 40 + colours * 4;
  std::string out(static_cast<size_t>(offset + stride * h), '\0');
  const auto put = [&](int at, uint32_t v, int n) {
    for (int i = 0; i < n; ++i) out[at + i] = static_cast<char>(v >> (8 * i));
  };
  out[0] = 'B';
  out[1] = 'M';
  put(2, static_cast<uint32_t>(out.size()), 4);
  put(10, static_cast<uint32_t>(offset), 4);
  put(14, 40, 4);
  put(18, static_cast<uint32_t>(w), 4);
  put(22, static_cast<uint32_t>(top_down ? -h : h), 4);
  put(26, 1, 2);
  put(28, static_cast<uint32_t>(bits), 2);
  put(46, static_cast<uint32_t>(colours), 4);
  for (int c = 0; c < colours; ++c) {
    const uint32_t v = bits == 1 ? (c ? 255 : 0) : static_cast<uint32_t>(c);
    put(54 + c * 4, v | v << 8 | v << 16, 4);
  }
  for (int i = 0; i < h; ++i) {
    const int y = top_down ? i : h - 1 - i;
    const uint8_t v = y < h / 2 ? 0 : 255;
    char* row = out.data() + offset + i * stride;
    for (int x = 0; x < w; ++x) {
      if (bits == 24) {
        row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = static_cast<char>(v);
      } else if (bits == 8) {
        row[x] = static_cast<char>(v);
      } else if (v != 0) {
        row[x / 8] = static_cast<char>(row[x / 8] | (0x80 >> (x % 8)));
      }
    }
  }
  return out;
}

void test_bmp_reader() {
  std::filesystem::remove_all("test-card-bmp");
  StdioStorage card("test-card-bmp");
  struct Kind {
    int bpp;
    bool top_down;
    bool fields;
  };
  for (const Kind k : {Kind{1, false, false}, Kind{4, false, false}, Kind{8, true, false}, Kind{24, false, false},
                       Kind{24, true, false}, Kind{32, false, false}, Kind{32, false, true}}) {
    const int w = 13, h = 5;  // an odd width: rows padded to 4 bytes
    const std::vector<uint8_t> file = make_bmp(k.bpp, w, h, k.top_down, k.fields);
    CHECK(card.write("p.bmp", file.data(), static_cast<uint32_t>(file.size())));
    File* f = card.open("p.bmp");
    CHECK(f != nullptr);
    if (f == nullptr) continue;
    bmp::Info info;
    CHECK(bmp::read_info(*f, info));
    CHECK(info.w == w && info.h == h && info.bpp == k.bpp && info.top_down == k.top_down);
    std::vector<uint8_t> raw(info.row_bytes), grey(w);
    bool same = true;
    for (int y = 0; y < h; ++y) {
      CHECK(bmp::read_row(*f, info, y, raw.data(), grey.data()));
      for (int x = 0; x < w; ++x) same = same && std::abs(grey[x] - bmp_grey(k.bpp, x, y)) <= 1;
    }
    if (!same) std::printf("BMP %d bits%s: wrong pixels\n", k.bpp, k.fields ? " (fields)" : "");
    CHECK(same);
    CHECK(!bmp::read_row(*f, info, h, raw.data(), grey.data()));
    card.close(f);
  }
  // Refused: compressed (RLE), 16 bits, not a BMP, cut short.
  std::vector<uint8_t> file = make_bmp(8, 4, 4, false);
  file[30] = 1;
  CHECK(card.write("p.bmp", file.data(), static_cast<uint32_t>(file.size())));
  auto refused = [&] {
    File* f = card.open("p.bmp");
    bmp::Info info;
    const bool ok = f != nullptr && bmp::read_info(*f, info);
    if (f != nullptr) card.close(f);
    return !ok;
  };
  CHECK(refused());
  file = make_bmp(24, 4, 4, false);
  file[28] = 16;
  CHECK(card.write("p.bmp", file.data(), static_cast<uint32_t>(file.size())));
  CHECK(refused());
  CHECK(card.write("p.bmp", "BMxx", 4));
  CHECK(refused());
  std::filesystem::remove_all("test-card-bmp");
}

// Counts the opening of the card's sleep pictures, to tell when one is decoded.
class PictureCountingCard : public StdioStorage {
 public:
  using StdioStorage::StdioStorage;
  File* open(const char* path) override {
    if (std::strncmp(path, "sleep/", 6) == 0) ++opened;
    return StdioStorage::open(path);
  }
  int opened = 0;
};

void copy_sleep_pictures(Storage& card) {
  for (const char* name : {"dawn.jpg", "field.png", "night.bmp"}) {
    const std::string data = read_all((std::string(VELLUM_TEST_DATA "/sleep/") + name).c_str());
    CHECK(!data.empty());
    CHECK(card.write((std::string("sleep/") + name).c_str(), data.data(), static_cast<uint32_t>(data.size())));
  }
}

void test_sleep_pictures() {
  std::filesystem::remove_all("test-card-sleep");
  PictureCountingCard card("test-card-sleep");
  SleepPictures pictures;
  pictures.scan(card);
  CHECK(pictures.count() == 0);
  CHECK(pictures.choose(card, SleepOrder::Shuffle, 1, true) == -1);  // none: the cover instead

  copy_sleep_pictures(card);
  CHECK(card.write("sleep/notes.txt", "x", 1));  // not a picture
  CHECK(is_picture_file("A.JPEG") && is_picture_file("b.Png") && !is_picture_file(".hidden.jpg"));
  pictures.scan(card);
  CHECK(pictures.count() == 3);
  if (pictures.count() != 3) return;
  CHECK(std::string(pictures.at(0).name) == "dawn.jpg" && std::string(pictures.at(2).name) == "night.bmp");

  // Each format converts to a screen picture, cut to the screen's shape and
  // never enlarged, with a thumbnail.
  std::vector<uint8_t> thumb_px(covers::kThumbW * covers::kThumbH);
  struct Want {
    int w, h, top, bottom;  // size and mean grey of the top and bottom fifths
  };
  const Want want[] = {{360, 600, 255, 0}, {300, 500, 255, 255}, {120, 200, 40, 40}};
  for (int i = 0; i < 3; ++i) {
    CHECK(!pictures.ready(card, i));
    CHECK(pictures.prepare(card, i));
    CHECK(pictures.ready(card, i));
    char path[64];
    pictures.cache_path(i, path, sizeof(path));
    CHECK(std::string(path).starts_with("vellum/sleep/"));
    const CoverImage* img = covers::load_screen(card, path);
    CHECK(img != nullptr);
    if (img == nullptr) continue;
    CHECK(img->w == want[i].w && img->h == want[i].h);
    CHECK(std::abs(band_mean(*img, 0, img->h / 5) - want[i].top) < 12);
    CHECK(std::abs(band_mean(*img, img->h * 4 / 5, img->h) - want[i].bottom) < 12);
    CoverImage thumb;
    thumb.px = thumb_px.data();
    CHECK(covers::load_thumb(card, path, thumb));
    CHECK(thumb.w > 0 && thumb.w <= covers::kThumbW && thumb.h > 0 && thumb.h <= covers::kThumbH);
    CHECK(thumb.h * 3 == thumb.w * 5 || std::abs(thumb.h * 3 - thumb.w * 5) <= 5);  // the screen's shape
  }

  // Sleeping again decodes nothing: the screen pictures are kept.
  card.opened = 0;
  for (int k = 0; k < 4; ++k) CHECK(pictures.choose(card, SleepOrder::Shuffle, 7, true) >= 0);
  CHECK(card.opened == 0);

  // Shuffle: never the same picture twice in a row, whatever the seed.
  pictures.scan(card);
  int last = pictures.choose(card, SleepOrder::Shuffle, 3, true);
  for (uint32_t seed = 0; seed < 12; ++seed) {
    pictures.scan(card);  // as each sleep does: the last one shown is read back
    const int next = pictures.choose(card, SleepOrder::Shuffle, seed / 3, true);
    CHECK(next >= 0 && next != last);
    last = next;
  }
  // Only among the marked ones when some are.
  pictures.set_mark(0, true);
  pictures.set_mark(2, true);
  CHECK(pictures.save(card));
  for (uint32_t seed = 0; seed < 6; ++seed) {
    pictures.scan(card);
    CHECK(pictures.marked_count() == 2);
    const int next = pictures.choose(card, SleepOrder::Shuffle, seed, true);
    CHECK((next == 0 || next == 2) && next != last);
    last = next;
  }
  // One picture: the marked one, every time; the first when none is marked.
  pictures.clear_marks();
  pictures.set_mark(1, true);
  for (uint32_t seed = 0; seed < 4; ++seed) CHECK(pictures.choose(card, SleepOrder::One, seed, true) == 1);
  pictures.clear_marks();
  CHECK(pictures.choose(card, SleepOrder::One, 5, true) == 0);

  // A broken picture is tried once, then skipped; the pinned one falls back to the others.
  CHECK(card.write("sleep/a-broken.jpg", "\xFF\xD8\xFF\x00junk", 8));
  pictures.scan(card);
  CHECK(pictures.count() == 4 && std::string(pictures.at(0).name) == "a-broken.jpg");
  pictures.set_mark(0, true);
  card.opened = 0;
  CHECK(!pictures.prepare(card, 0));
  CHECK(pictures.ready(card, 0) && card.opened == 1);
  CHECK(!pictures.prepare(card, 0));
  CHECK(card.opened == 1);
  CHECK(pictures.choose(card, SleepOrder::One, 0, false) == 1);

  // Gone or changed pictures lose their screen pictures.
  char gone[64], changed[64];
  pictures.cache_path(3, gone, sizeof(gone));
  pictures.cache_path(1, changed, sizeof(changed));
  std::remove("test-card-sleep/sleep/night.bmp");
  const std::string dawn = read_all(VELLUM_TEST_DATA "/sleep/dawn.jpg") + "extra";
  CHECK(card.write("sleep/dawn.jpg", dawn.data(), static_cast<uint32_t>(dawn.size())));
  pictures.scan(card);
  CHECK(pictures.count() == 3);
  CHECK(!std::filesystem::exists(std::string("test-card-sleep/") + gone));
  CHECK(!std::filesystem::exists(std::string("test-card-sleep/") + changed));
  CHECK(!pictures.ready(card, 1));

  // Only broken pictures: nothing to show.
  std::filesystem::remove_all("test-card-sleep/sleep");
  CHECK(card.write("sleep/x.png", "\x89PNG\r\n\x1a\nbad", 11));
  CHECK(card.write("sleep/y.bmp", "BM", 2));
  pictures.scan(card);
  CHECK(pictures.count() == 2);
  CHECK(pictures.choose(card, SleepOrder::Shuffle, 0, true) == -1);
  std::filesystem::remove_all("test-card-sleep");
}

// Share of black pixels in a logical rectangle, in percent.
int black_percent(const Framebuffer& fb, Rect r) {
  int n = 0;
  for (int y = r.y; y < r.y + r.h; ++y)
    for (int x = r.x; x < r.x + r.w; ++x) n += fb.pixel(x, y) == Color::Black ? 1 : 0;
  return n * 100 / (r.w * r.h);
}

void test_app_sleep_screens() {
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer view(buf.data(), kW, kH);
  view.set_rotation(Rotation::R90);
  CountingPanel panel;
  NoInput input;
  TestPower power;
  std::filesystem::remove_all("test-card-sleep");
  PictureCountingCard card("test-card-sleep");
  const Rect whole{0, 0, kH, kW};
  const Rect corner{kH / 2 - 20, kW - 36, 40, 20};  // the "Page as left" badge
  auto set_kind = [&](int kind, int order = 0) {
    char text[64];
    const int n = std::snprintf(text, sizeof(text), "sleep_screen=%d\nsleep_order=%d\n", kind, order);
    CHECK(card.write("vellum/settings.txt", text, static_cast<uint32_t>(n)));
  };
  auto hold_power = [](App& app) {
    app.handle(key_event(InputEvent::Type::KeyDown, Key::Power));
    app.timeout();
    app.handle(key_event(InputEvent::Type::KeyUp, Key::Power));
  };

  // Every kind sleeps without a card, on an empty card, and with pictures.
  for (int pass = 0; pass < 3; ++pass) {
    if (pass == 2) copy_sleep_pictures(card);
    for (int kind = 0; kind < 5; ++kind) {
      Board board{"test",        panel,     input, power, nullptr, nullptr, pass == 0 ? nullptr : &card,
                  Rotation::R90, buf.data()};
      if (pass > 0) set_kind(kind);
      auto* app = new App(board);
      app->start();
      const int sleeps = power.sleeps;
      hold_power(*app);
      CHECK(power.sleeps == sleeps + 1);
      app->handle(key_event(InputEvent::Type::KeyDown, Key::Next));  // wakes
      delete app;
    }
  }

  // The book cover, then a picture in its place; the cover again when no picture decodes.
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  set_kind(0);
  auto* app = new App(board);
  app->start();
  hold_power(*app);
  const std::vector<uint8_t> cover = buf;
  delete app;
  std::filesystem::remove_all("test-card-sleep/sleep");
  const std::string night = read_all(VELLUM_TEST_DATA "/sleep/night.bmp");
  CHECK(card.write("sleep/night.bmp", night.data(), static_cast<uint32_t>(night.size())));
  set_kind(1);
  app = new App(board);
  app->start();
  hold_power(*app);
  CHECK(black_percent(view, whole) > 70);  // dark grey, dithered
  card.opened = 0;
  app->handle(key_event(InputEvent::Type::KeyDown, Key::Next));
  hold_power(*app);  // the second sleep reads the kept screen picture
  CHECK(card.opened == 0 && black_percent(view, whole) > 70);
  delete app;
  CHECK(card.write("sleep/night.bmp", "BMjunk", 6));
  app = new App(board);
  app->start();
  hold_power(*app);
  CHECK(buf == cover);
  delete app;

  // Minimal: a white page; reading stats: some ink.
  set_kind(3);
  app = new App(board);
  app->start();
  hold_power(*app);
  CHECK(black_percent(view, whole) < 10);
  delete app;
  set_kind(2);
  app = new App(board);
  app->start();
  hold_power(*app);
  const int stats_ink = black_percent(view, whole);
  CHECK(stats_ink > 0 && stats_ink < 30);
  delete app;

  // Page as left: the page stays, with a badge in the corner.
  set_kind(4);
  app = new App(board);
  app->start();
  press(*app, Key::Power);  // open the current book
  std::vector<uint8_t> page = buf;
  CHECK(black_percent(view, corner) < 30);
  hold_power(*app);
  CHECK(black_percent(view, corner) > 50);
  Framebuffer before(page.data(), kW, kH);
  before.set_rotation(Rotation::R90);
  int differ = 0;
  for (int y = 0; y < kW - 50; ++y)
    for (int x = 0; x < kH; ++x) differ += before.pixel(x, y) != view.pixel(x, y) ? 1 : 0;
  CHECK(differ == 0);
  delete app;

  // An empty battery adds its band to every kind.
  for (int kind = 0; kind < 5; ++kind) {
    set_kind(kind);
    power.level = 1;
    app = new App(board);
    app->start();                                            // straight to sleep
    CHECK(view.pixel(kH / 2, kW - 120) == Color::Black);     // the band's edge
    CHECK(black_percent(view, {0, kW - 118, kH, 40}) < 10);  // white under it
    CHECK(black_percent(view, {0, kW - 90, kH, 40}) > 0);    // its message
    power.level = 50;
    delete app;
  }

  // Settings › Sleep screen: choose Minimal, preview it, choose a picture.
  copy_sleep_pictures(card);
  set_kind(0);
  std::remove("test-card-sleep/vellum/state.txt");  // start at home, not in the book
  app = new App(board);
  app->start();
  press(*app, Key::Prev);  // Settings, the last link
  press(*app, Key::Power);
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);  // Language, Menu font, Clock, Sleep after: Sleep screen
  press(*app, Key::Power);
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);  // Book cover, My pictures, Reading stats: Minimal
  press(*app, Key::Power);
  CHECK(read_all("test-card-sleep/vellum/settings.txt").find("sleep_screen=3") != std::string::npos);
  for (int i = 0; i < 4; ++i) press(*app, Key::Next);  // Page as left, Pictures, Choose pictures: Preview
  const int full = panel.full;
  press(*app, Key::Power);
  CHECK(panel.full == full + 1);
  CHECK(black_percent(view, {0, 80, kH, 300}) < 10);  // the minimal page
  CHECK(black_percent(view, {28, 70, 300, 40}) == 0);
  press(*app, Key::Next);  // any button: back
  CHECK(panel.full == full + 2);
  CHECK(black_percent(view, {28, 70, 300, 40}) > 3);  // the settings' title
  press(*app, Key::Prev);                             // Choose pictures
  press(*app, Key::Power);
  press(*app, Key::Next);   // field.png
  press(*app, Key::Power);  // marked
  CHECK(read_all("test-card-sleep/vellum/sleep.txt").find("mark\tfield.png") != std::string::npos);
  press(*app, Key::Home);  // back to the sleep screen settings
  press(*app, Key::Prev);  // Pictures: Shuffle becomes One picture
  press(*app, Key::Power);
  CHECK(read_all("test-card-sleep/vellum/settings.txt").find("sleep_order=1") != std::string::npos);
  press(*app, Key::Home);  // back to Settings, on the Sleep screen row
  press(*app, Key::Home);
  delete app;
  std::filesystem::remove_all("test-card-sleep");
}

void test_library_folders() {
  std::filesystem::remove_all("test-card-folders");
  StdioStorage card("test-card-folders");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  for (const char* path :
       {"books/a.epub", "books/X/b.epub", "books/X/Y/c.epub", "books/X/Y/Z/d.epub", "e.epub", "Root/f.epub",
        "Root/F/g.epub", "Root/F/G/h.epub", "vellum/i.epub", "fonts/j.epub", ".hidden/k.epub", "books/.trash/l.epub"}) {
    write_file(card, path, book);
  }
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  // Two folders down from books/ and from the root; not deeper, not Vellum's own, not hidden.
  for (const char* path :
       {"books/a.epub", "books/X/b.epub", "books/X/Y/c.epub", "e.epub", "Root/f.epub", "Root/F/g.epub"}) {
    CHECK(lib.find(path) >= 0);
  }
  for (const char* path : {"books/X/Y/Z/d.epub", "Root/F/G/h.epub", "vellum/i.epub", "fonts/j.epub", ".hidden/k.epub",
                           "books/.trash/l.epub"}) {
    CHECK(lib.find(path) < 0);
  }
  CHECK(lib.count() == 4 + 6);
  CHECK(lib.folder_count() == 4);  // books/X, books/X/Y, Root, Root/F
  CHECK(Library::folder_depth(".") == 0 && Library::folder_depth("books") == 0);
  CHECK(Library::folder_depth("books/X/Y") == 2 && Library::folder_depth("Root/F") == 2);
  CHECK(Library::folder_depth("Root/F/G") == 3 && Library::folder_depth("booksellers") == 1);
  CHECK(Library::shown_folder(".") && Library::shown_folder("books/X/Y") && Library::shown_folder("Root"));
  CHECK(!Library::shown_folder("books/X/Y/Z") && !Library::shown_folder("Vellum/covers") &&
        !Library::shown_folder("Root/.git") && !Library::shown_folder("dictionaries"));

  // At most kMaxFolders folders are read, however many there are.
  for (int i = 0; i < 80; ++i) write_file(card, ("books/many/N" + std::to_string(i) + "/m.epub").c_str(), book);
  Library many;
  many.load(builtin_books(), &card, epub);
  CHECK(many.folder_count() == Library::kMaxFolders - 2);
  CHECK(many.count() > 4 + 6 && many.count() < 4 + 6 + 80);
  std::filesystem::remove_all("test-card-folders");
}

void test_recently_added() {
  std::filesystem::remove_all("test-card-added");
  StdioStorage card("test-card-added");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  write_file(card, "books/b.epub", book);
  write_file(card, "books/a.epub", book);
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  int order[Library::kMaxBooks];
  CHECK(lib.sorted(LibrarySort::Added, order, Library::kMaxBooks) == 6);
  // The card's books first, numbered as found; the samples after them, by title.
  CHECK(lib.entry(order[0]).added == 2 && lib.entry(order[1]).added == 1);
  CHECK(std::string(lib.entry(order[2]).title).starts_with("Alice") && lib.entry(order[5]).builtin != nullptr);
  const uint32_t a_added = lib.entry(lib.find("books/a.epub")).added;

  // A book that arrives later comes first; the others keep their numbers.
  write_file(card, "books/c.epub", book);
  Library again;
  again.load(builtin_books(), &card, epub);
  again.sorted(LibrarySort::Added, order, Library::kMaxBooks);
  CHECK(std::string(again.entry(order[0]).path) == "books/c.epub" && again.entry(order[0]).added == 3);
  CHECK(again.entry(again.find("books/a.epub")).added == a_added);

  // A cache from before the numbers (six fields) numbers its books once more.
  std::string cache = read_all("test-card-added/vellum/library.txt");
  std::string old;
  for (size_t at = 0; at < cache.size();) {
    const size_t end = cache.find('\n', at);
    const std::string line = cache.substr(at, end - at);
    old += line.substr(0, line.rfind('\t')) + "\n";
    at = end + 1;
  }
  write_file(card, "vellum/library.txt", old);
  Library older;
  older.load(builtin_books(), &card, epub);
  for (int i = 0; i < older.count(); ++i) CHECK((older.entry(i).builtin != nullptr) == (older.entry(i).added == 0));
  CHECK(count_lines(read_all("test-card-added/vellum/library.txt")) == 3);
  CHECK(read_all("test-card-added/vellum/library.txt").find("\t2\t3\n") != std::string::npos);

  // In the app, Sort goes Recent, Title, Author, Added and round again.
  Settings s;
  s.library_sort = LibrarySort::Added;
  CHECK(save_settings(card, s));
  Settings loaded;
  load_settings(card, loaded);
  CHECK(loaded.library_sort == LibrarySort::Added);
  std::filesystem::remove_all("test-card-added");
}

void test_pictures() {
  std::filesystem::remove_all("test-card-pictures");
  StdioStorage card("test-card-pictures");
  CHECK(is_picture_file("a.JPG") && is_picture_file("b.jpeg") && is_picture_file("c.Png") && is_picture_file("d.bmp"));
  CHECK(!is_picture_file("e.gif") && !is_picture_file("jpg") && !is_picture_file("f.epub"));
  std::vector<uint8_t> px(covers::kLargeW * covers::kLargeH);
  CoverImage img;
  img.px = px.data();
  // Bitmaps of each kind, either way up: fitted into the large thumbnail's box.
  for (int bits : {1, 8, 24}) {
    for (bool top_down : {false, true}) {
      write_file(card, "pictures/p.bmp", make_half_bmp(600, 300, bits, top_down));
      CHECK(covers::load_picture(card, "pictures/p.bmp", img));
      CHECK(img.w == covers::kLargeW && img.h == 200);
      if (!img.valid()) continue;
      CHECK(band_mean(img, 0, img.h / 4) < 30);
      CHECK(band_mean(img, img.h * 3 / 4, img.h) > 225);
    }
  }
  // The sample books' covers as picture files (white top, black bottom):
  // a JPEG read as it decodes, and a PNG.
  StdioStorage data(VELLUM_TEST_DATA);
  for (auto [name, file] :
       {std::pair{"sample.epub", "pictures/c.jpg"}, std::pair{"sample-ncx.epub", "pictures/c.png"}}) {
    Epub epub;
    CHECK(epub.open(data, name));
    const uint8_t* picture = nullptr;
    const int32_t len = epub.read_cover(&picture);
    CHECK(len > 0);
    if (len <= 0) continue;
    CHECK(card.write(file, picture, static_cast<uint32_t>(len)));
    CHECK(covers::load_picture(card, file, img));
    if (!img.valid()) continue;
    CHECK(band_mean(img, 0, img.h / 5) > 220);
    CHECK(band_mean(img, img.h * 9 / 10, img.h) < 40);
  }
  // Not pictures, or not whole.
  write_file(card, "pictures/junk.jpg", std::string("\xFF\xD8\xFF\x00junk", 8));
  CHECK(!covers::load_picture(card, "pictures/junk.jpg", img) && !img.valid());
  write_file(card, "pictures/cut.bmp", make_half_bmp(600, 300, 24, false).substr(0, 4000));
  CHECK(!covers::load_picture(card, "pictures/cut.bmp", img));
  CHECK(!covers::load_picture(card, "pictures/missing.png", img));
  std::filesystem::remove_all("test-card-pictures");
}

void test_app_move_finished() {
  std::filesystem::remove_all("test-card-move");
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-move");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  write_file(card, "books/sample.epub", book);
  write_file(card, "vellum/settings.txt", "move_finished=1\n");
  write_file(card, "vellum/progress.txt", "books/sample.epub\t0\t0\t0\t0\t1\n");  // the book read last
  write_file(card, "vellum/bookmarks.txt", "books/sample.epub\t0\t0\t0\t5\tFirst words\n");
  {
    // Everything else that names the book by its path.
    Shelves shelves;
    shelves.add("Poezija");
    shelves.set(0, "books/sample.epub", true);
    CHECK(shelves.save(card));
    Highlights highlights;
    Highlight h;
    std::snprintf(h.path, sizeof(h.path), "books/sample.epub");
    std::snprintf(h.text, sizeof(h.text), "a passage");
    CHECK(highlights.add(h) == 0 && highlights.save(card));
    ReadingStats stats;
    stats.start("books/sample.epub", 0);
    stats.turn(30000, -1, true);
    stats.stop(60000, -1);
    CHECK(stats.save(card));
  }
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  const auto file = [](const char* name) { return read_all((std::string("test-card-move/vellum/") + name).c_str()); };

  // Read to the end: finished, but the file stays while the book is open.
  press(*app, Key::Power);
  for (int i = 0; i < 40; ++i) press(*app, Key::Next);
  CHECK(file("progress.txt").find("books/sample.epub\t") != std::string::npos);
  CHECK(std::filesystem::exists("test-card-move/books/sample.epub"));

  // Leaving it for home moves it, and every record follows.
  press(*app, Key::Home);
  CHECK(!std::filesystem::exists("test-card-move/books/sample.epub"));
  CHECK(std::filesystem::exists("test-card-move/books/Read/sample.epub"));
  for (const char* name :
       {"progress.txt", "bookmarks.txt", "highlights.txt", "shelves.txt", "stats.txt", "library.txt"}) {
    const std::string text = file(name);
    if (text.find("books/Read/sample.epub") == std::string::npos) std::printf("not moved in %s\n", name);
    CHECK(text.find("books/Read/sample.epub") != std::string::npos);
    CHECK(text.find("books/sample.epub") == std::string::npos);
  }
  CHECK(file("progress.txt").find("books/Read/sample.epub\t") != std::string::npos);
  CHECK(file("progress.txt").find("\t1\n") != std::string::npos);  // still finished
  char cover[64];
  covers::path_for("books/Read/sample.epub", static_cast<uint32_t>(book.size()), cover, sizeof(cover));
  CHECK(std::filesystem::exists(std::string("test-card-move/") + cover));

  // It opens again from its new place.
  const int full = panel.full;
  press(*app, Key::Power);
  CHECK(panel.full == full + 1);
  press(*app, Key::Home);
  delete app;

  // A name taken in Read/ gets a number.
  write_file(card, "books/sample.epub", book);
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  const int again = lib.find("books/sample.epub");
  CHECK(again >= 0 && lib.move_book(card, again, Library::kReadFolder));
  CHECK(std::string(lib.entry(again).path) == "books/Read/sample (2).epub");
  CHECK(std::filesystem::exists("test-card-move/books/Read/sample (2).epub"));
  CHECK(!lib.move_book(card, 0, Library::kReadFolder));  // a sample has no file
  std::filesystem::remove_all("test-card-move");
}

void test_app_cover_grid() {
  std::filesystem::remove_all("test-card-grid");
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer screen(buf.data(), kW, kH);
  screen.set_rotation(Rotation::R90);
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-grid");
  const std::string book = read_all(VELLUM_TEST_DATA "/sample.epub");
  for (int i = 0; i < 8; ++i) write_file(card, ("books/copy" + std::to_string(i) + ".epub").c_str(), book);
  write_file(card, "vellum/settings.txt", "library_view=1\nlibrary_sort=1\n");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  // The same library the app sees: Alice, Moby-Dick, Persuasion, eight copies, Father Brown.
  Epub epub;
  Library lib;
  lib.load(builtin_books(), &card, epub);
  CHECK(lib.count() == 12);
  const auto opened = [&](int i) {
    return read_all("test-card-grid/vellum/progress.txt").find(std::string(lib.entry(i).path) + "\t") !=
           std::string::npos;
  };
  // Tiles are a third of the width between the margins, 160 px tall.
  const auto tile = [](int top, int slot, int& x, int& y) {
    x = 16 + slot % 3 * 149 + 74;
    y = top + slot / 3 * 160 + 60;
  };
  int x, y;

  // Home: nine covers, nothing read yet so in library order. Tapping the third opens it.
  tile(112, 2, x, y);
  tap(*app, x, y);
  CHECK(opened(2));
  press(*app, Key::Home);
  // Now it comes first; the side buttons go through the covers: the third is Moby-Dick.
  press(*app, Key::Next);
  press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(opened(1));
  press(*app, Key::Home);

  // After the last cover, All books. The library pages nine covers at a time.
  for (int i = 0; i < 9; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  CHECK(screen.pixel(90, 233) == Color::Black);        // the first cover is selected: inverted
  CHECK(screen.pixel(90 + 149, 233) == Color::White);  // the next is not
  press(*app, Key::Next);
  CHECK(screen.pixel(90, 233) == Color::White && screen.pixel(90 + 149, 233) == Color::Black);
  swipe(*app, 400, 500, 100, 510);  // the second page: the tenth book first
  tile(230, 0, x, y);
  tap(*app, x, y);
  CHECK(opened(9));
  press(*app, Key::Home);

  // Holding a cover opens its shelves, as in the list.
  for (int i = 0; i < 9; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  tile(230, 4, x, y);
  hold(*app, x, y);
  press(*app, Key::Next);
  press(*app, Key::Power);  // Finished reading
  CHECK(read_all("test-card-grid/vellum/progress.txt").find(std::string(lib.entry(4).path) + "\t0\t0\t0\t0\t0\t1\n") !=
        std::string::npos);
  delete app;
  std::filesystem::remove_all("test-card-grid");
}

void test_app_folders_and_pictures() {
  std::filesystem::remove_all("test-card-browse");
  std::vector<uint8_t> buf(Framebuffer::bytes_for(kW, kH));
  Framebuffer screen(buf.data(), kW, kH);
  screen.set_rotation(Rotation::R90);
  CountingPanel panel;
  NoInput input;
  FixedPower power;
  StdioStorage card("test-card-browse");
  write_file(card, "books/X/b.epub", read_all(VELLUM_TEST_DATA "/sample.epub"));
  write_file(card, "pictures/a.bmp", make_half_bmp(600, 300, 24, false));  // black top
  write_file(card, "pictures/b.bmp", make_half_bmp(600, 300, 8, true));
  write_file(card, "pictures/c.jpg", std::string("\xFF\xD8\xFF\x00junk", 8));  // cannot be shown
  write_file(card, "pictures/readme.md", "not shown");
  Board board{"test", panel, input, power, nullptr, nullptr, &card, Rotation::R90, buf.data()};
  auto* app = new App(board);
  app->start();
  // The selected row of the folder list is inverted; rows are 76 px from y = 160.
  const auto row_selected = [&](int row) { return screen.pixel(455, 160 + row * 76 + 38) == Color::Black; };

  // The library's Folders tab, after Finished: from the first book, Prev twice.
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
  press(*app, Key::Power);
  press(*app, Key::Prev);
  press(*app, Key::Prev);
  press(*app, Key::Power);
  CHECK(row_selected(0) && !row_selected(1));  // books, pictures
  press(*app, Key::Next);
  press(*app, Key::Power);  // pictures: up, a.bmp, b.bmp, c.jpg
  CHECK(row_selected(1));
  CHECK(screen.pixel(455, 160 + 4 * 76 + 38) == Color::White);  // no row for readme.md

  // A picture fills the width, in the middle; the side buttons step through them.
  press(*app, Key::Power);
  CHECK(screen.pixel(240, 300) == Color::Black && screen.pixel(240, 500) == Color::White);
  CHECK(screen.pixel(240, 100) == Color::White);
  press(*app, Key::Next);  // b.bmp, the same picture stored the other way up
  CHECK(screen.pixel(240, 300) == Color::Black && screen.pixel(240, 500) == Color::White);
  press(*app, Key::Next);  // c.jpg cannot be shown: a message instead
  CHECK(screen.pixel(240, 300) == Color::White && screen.pixel(240, 500) == Color::White);
  press(*app, Key::Next);  // round to a.bmp
  CHECK(screen.pixel(240, 300) == Color::Black);
  swipe(*app, 100, 400, 400, 410);  // back: c.jpg
  CHECK(screen.pixel(240, 300) == Color::White);
  tap(*app, 240, 400);  // the middle: back to the folder, on that picture
  CHECK(row_selected(3));

  // Home goes up a folder, onto the one left; at the top, back to the library.
  press(*app, Key::Home);
  CHECK(row_selected(1) && !row_selected(0));
  press(*app, Key::Home);
  press(*app, Key::Power);  // the Folders tab is still selected
  CHECK(row_selected(0));

  // A book in a folder opens from there.
  press(*app, Key::Power);  // books: up, X
  press(*app, Key::Power);  // X: up, b.epub
  CHECK(row_selected(1));
  const int full = panel.full;
  press(*app, Key::Power);
  CHECK(panel.full == full + 1);
  CHECK(read_all("test-card-browse/vellum/progress.txt").find("books/X/b.epub\t") != std::string::npos);
  delete app;
  std::filesystem::remove_all("test-card-browse");
}

int main() {
  test_rotation_mapping();
  test_rotation_round_trip();
  test_gestures();
  test_app_touch();
  test_fill_rect_matches_set_pixel();
  test_fill_rect_clips();
  test_display_queue_cadence();
  test_utf8();
  test_font_lookup_and_kerning();
  test_pagination_covers_text_once();
  test_hyphenation();
  test_hyphenation_matches_reference();
  test_library_order();
  test_time_format();
  test_book_count_plurals();
  test_interface_text_fits();
  test_upload_helpers();
  test_upload_page_embedded();
  test_xml_entities();
  test_epub();
  test_epub_contents();
  test_card_library_and_progress();
  test_covers();
  test_app_transfer();
  test_app_update();
  test_update_helpers();
  test_app_sleep();
  test_app_contents_and_bookmarks();
  test_dates_and_battery_log();
  test_app_battery_log();
  test_app_front_light();
  test_app_device_check();
  test_font_files();
  test_raster_font();
  test_font_library();
  test_app_fonts();
  test_app_updates_screen();
  test_app_night_update();
  test_app_device_check_memory();
  test_app_empty_battery();
  test_app_after_crash();
  test_peer_transfer();
  test_app_send_book();
  test_reader_crosses_chapters();
  test_reading_style_changes_layout();
  test_long_press();
  test_word_boxes();
  test_word_forms();
  test_stardict();
  test_highlights_store();
  test_app_words();
  test_dictionary_upload_names();
  test_reading_stats();
  test_reader_pages_left();
  test_reader_long_chapter();
  test_reader_relayout_rendered_fonts();
  test_app_reading_stats();
  test_charset_detection();
  test_text_headings();
  test_text_paragraphs();
  test_text_book_1257();
  test_text_book_long_and_named();
  test_fb2_book();
  test_fb2_zip_book();
  test_html_book();
  test_library_finds_other_formats();
  test_book_upload_names();
  test_app_opens_fb2();
  test_opds_gutenberg();
  test_opds_standard_ebooks();
  test_opds_details();
  test_store_download();
  test_app_store();
  test_app_store_search();
  test_keyboard();
  test_search_folding();
  test_library_sorting();
  test_shelves_store();
  test_app_library();
  test_button_sets();
  test_app_buttons();
  test_app_orientation();
  test_app_auto_turn();
  test_app_refresh_cadence();
  test_screenshot_bmp();
  test_bmp_reader();
  test_sleep_pictures();
  test_app_sleep_screens();
  test_library_folders();
  test_recently_added();
  test_pictures();
  test_app_move_finished();
  test_app_cover_grid();
  test_app_folders_and_pictures();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}

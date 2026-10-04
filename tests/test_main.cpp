// Host unit tests for the portable core. Plain asserts, no framework.
#include <cstdio>
#include <string>
#include <vector>

#include "vellum/book.h"
#include "vellum/display_queue.h"
#include "vellum/epub.h"
#include "vellum/framebuffer.h"
#include "vellum/library.h"
#include "vellum/reader.h"
#include "vellum/settings.h"
#include "vellum/stdio_storage.h"
#include "vellum/strings.h"
#include "vellum/text.h"
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
    for (const char* hint : {t.hint_home, t.hint_list, t.hint_settings}) {
      const int w = text_width_px(fonts::literata_16, hint);
      if (w > kH - 2 * 28) std::printf("too wide (%d px): %s\n", w, hint);
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

}  // namespace

int main() {
  test_rotation_mapping();
  test_fill_rect_matches_set_pixel();
  test_fill_rect_clips();
  test_display_queue_cadence();
  test_utf8();
  test_font_lookup_and_kerning();
  test_pagination_covers_text_once();
  test_hyphenation();
  test_library_order();
  test_time_format();
  test_book_count_plurals();
  test_interface_text_fits();
  test_xml_entities();
  test_epub();
  test_card_library_and_progress();
  test_reader_crosses_chapters();
  test_reading_style_changes_layout();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}

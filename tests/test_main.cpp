// Host unit tests for the portable core. Plain asserts, no framework.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "vellum/app.h"
#include "vellum/battery_log.h"
#include "vellum/book.h"
#include "vellum/covers.h"
#include "vellum/display_queue.h"
#include "vellum/epub.h"
#include "vellum/framebuffer.h"
#include "vellum/library.h"
#include "vellum/reader.h"
#include "vellum/settings.h"
#include "vellum/stdio_storage.h"
#include "vellum/strings.h"
#include "vellum/text.h"
#include "vellum/touch.h"
#include "vellum/ui.h"
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
    for (const char* hint :
         {t.hint_home, t.hint_list, t.hint_settings, t.hint_transfer, t.transfer_lead, t.hint_battery, t.hint_light}) {
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
  CHECK(safe("notes.txt") == "<rejected>");
  CHECK(safe(".epub") == "<rejected>");
  CHECK(safe("") == "<rejected>");
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

  // Rows now: back, remove, light, BOOKMARKS, the bookmark, CHAPTERS, three entries.
  press(*app, Key::Power);
  for (int i = 0; i < 6; ++i) press(*app, Key::Next);  // labels are skipped
  press(*app, Key::Power);                             // "Second"
  CHECK(progress_has("books/sample.epub\t1\t0\t0\t"));

  press(*app, Key::Power);
  press(*app, Key::Next);
  press(*app, Key::Power);  // bookmark this page too
  CHECK(count_lines(read_all("test-card/vellum/bookmarks.txt")) == 2);

  // Back to the first bookmark (the bookmarks come in reading order), then remove it.
  press(*app, Key::Power);
  for (int i = 0; i < 3; ++i) press(*app, Key::Next);
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

}  // namespace

// Mean grey of a band of rows in a thumbnail.
int band_mean(const CoverImage& img, int y0, int y1) {
  long sum = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < img.w; ++x) sum += img.px[y * img.w + x];
  return static_cast<int>(sum / ((y1 - y0) * img.w));
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
  test_app_sleep();
  test_app_contents_and_bookmarks();
  test_dates_and_battery_log();
  test_app_battery_log();
  test_app_front_light();
  test_reader_crosses_chapters();
  test_reading_style_changes_layout();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}

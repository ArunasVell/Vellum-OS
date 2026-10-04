// Host unit tests for the portable core. Plain asserts, no framework.
#include <cstdio>
#include <string>
#include <vector>

#include "vellum/book.h"
#include "vellum/display_queue.h"
#include "vellum/framebuffer.h"
#include "vellum/library.h"
#include "vellum/settings.h"
#include "vellum/text.h"

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
  Library lib(builtin_books());
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
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}

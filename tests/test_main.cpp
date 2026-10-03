// Host unit tests for the portable core. Plain asserts, no framework.
#include <cstdio>
#include <vector>

#include "vellum/display_queue.h"
#include "vellum/framebuffer.h"

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

}  // namespace

int main() {
  test_rotation_mapping();
  test_fill_rect_matches_set_pixel();
  test_fill_rect_clips();
  test_display_queue_cadence();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}

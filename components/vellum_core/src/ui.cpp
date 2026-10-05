#include "vellum/ui.h"

#include <algorithm>
#include <cstdio>

#include "qrcodegen.h"
#include "vellum/text.h"

namespace vellum::ui {
namespace {

MenuFonts g_menu = literata_menu();

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // …
constexpr int32_t kLabelTracking = 96;                  // 1.5 px between label letters, 1/64 px

// How much of `text` fits in max_w px, with an ellipsis when it is cut.
struct Fit {
  size_t length;
  bool ellipsis;
};

Fit fit(const Font& font, std::string_view text, int max_w) {
  const int32_t room = max_w * 64;
  if (text_width(font, text) <= room) return {text.size(), false};
  const int32_t budget = room - text_width(font, kEllipsis);
  size_t best = 0;
  size_t i = 0;
  while (i < text.size()) {
    decode_utf8(text, i);
    if (text_width(font, text.substr(0, i)) > budget) break;
    best = i;
  }
  while (best > 0 && (text[best - 1] == ' ' || text[best - 1] == ',')) --best;
  return {best, true};
}

int fit_width_px(const Font& font, std::string_view text, Fit f) {
  return (text_width(font, text.substr(0, f.length)) + (f.ellipsis ? text_width(font, kEllipsis) : 0) + 63) / 64;
}

void draw_fit(Framebuffer& fb, const Font& font, int x, int baseline, std::string_view text, Fit f, Color c) {
  const int32_t end = draw_text(fb, font, x * 64, baseline, text.substr(0, f.length), c);
  if (f.ellipsis) draw_text(fb, font, end, baseline, kEllipsis, c);
}

uint32_t hash(std::string_view s) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (char ch : s) h = (h ^ static_cast<uint8_t>(ch)) * 16777619u;
  return h;
}

// First letter of a title, skipping a leading "The ".
uint32_t initial(std::string_view title) {
  if (title.starts_with("The ") && title.size() > 4) title.remove_prefix(4);
  size_t i = 0;
  return title.empty() ? '?' : decode_utf8(title, i);
}

}  // namespace

void draw_centered(Framebuffer& fb, const Font& font, int baseline, std::string_view text, Color c) {
  draw_text(fb, font, (fb.width() - text_width_px(font, text)) / 2 * 64, baseline, text, c);
}

void draw_right(Framebuffer& fb, const Font& font, int right, int baseline, std::string_view text, Color c) {
  draw_text(fb, font, (right - text_width_px(font, text)) * 64, baseline, text, c);
}

void draw_fitted(Framebuffer& fb, const Font& font, int x, int baseline, int max_w, std::string_view text, Color c) {
  draw_fit(fb, font, x, baseline, text, fit(font, text, max_w), c);
}

int draw_wrapped(Framebuffer& fb, const Font& font, int x, int baseline, int max_w, int max_lines,
                 std::string_view text, bool centered, Color c) {
  size_t start = 0;
  for (int line = 0; line < max_lines && start < text.size(); ++line) {
    // Take whole words while they fit; always at least one.
    size_t end = text.find(' ', start);
    if (end == std::string_view::npos) end = text.size();
    for (;;) {
      if (end >= text.size()) break;
      size_t next = text.find(' ', end + 1);
      if (next == std::string_view::npos) next = text.size();
      if (text_width(font, text.substr(start, next - start)) > max_w * 64) break;
      end = next;
    }
    // The last allowed line takes the rest of the text, cut with an ellipsis.
    const std::string_view part = text.substr(start, (line + 1 == max_lines ? text.size() : end) - start);
    const Fit f = fit(font, part, max_w);
    const int lx = centered ? x + (max_w - fit_width_px(font, part, f)) / 2 : x;
    draw_fit(fb, font, lx, baseline, part, f, c);
    baseline += font.line_height;
    start = end;
    while (start < text.size() && text[start] == ' ') ++start;
  }
  return baseline;
}

void draw_label(Framebuffer& fb, int x, int baseline, std::string_view text) {
  const Font& font = label_font();
  int32_t pen = x * 64;
  size_t i = 0;
  while (i < text.size()) {
    const size_t begin = i;
    decode_utf8(text, i);
    pen = draw_text(fb, font, pen, baseline, text.substr(begin, i - begin)) + kLabelTracking;
  }
}

void draw_battery(Framebuffer& fb, int right, int baseline, int percent) {
  if (percent < 0) return;
  const Rect body{right - 24, baseline - 12, 21, 12};
  fb.draw_rect(body, Color::Black);
  fb.fill_rect({body.x + body.w, body.y + 3, 2, 5}, Color::Black);
  const int fill = (body.w - 4) * std::clamp(percent, 0, 100) / 100;
  fb.fill_rect({body.x + 2, body.y + 2, fill, body.h - 4}, Color::Black);
  char label[16];
  std::snprintf(label, sizeof(label), "%d%%", std::clamp(percent, 0, 100));
  draw_right(fb, label_font(), body.x - 6, baseline, label);
}

void draw_status_bar(Framebuffer& fb, std::string_view time, int battery_percent) {
  draw_text(fb, label_font(), kSide * 64, kStatusBase, time);
  draw_battery(fb, fb.width() - kSide, kStatusBase, battery_percent);
  fb.hline(kSide, kStatusRule, fb.width() - 2 * kSide, Color::Black);
}

void draw_hint(Framebuffer& fb, std::string_view text) {
  draw_centered(fb, label_font(), fb.height() - kHintBase, text);
}

void draw_progress(Framebuffer& fb, int x, int y, int w, int done, int total) {
  for (int dx = 0; dx < w; dx += 4) fb.set_pixel(x + dx, y, Color::Black);
  if (total > 0) fb.fill_rect({x, y - 1, w * std::clamp(done, 0, total) / total, 3}, Color::Black);
}

void draw_picture(Framebuffer& fb, Rect r, const CoverImage& p) {
  if (r.w <= 0 || r.h <= 0 || !p.valid()) return;
  // The part of the picture that fills r at one scale: all of one side,
  // the middle of the other.
  int src_w = p.w, src_h = p.h;
  if (int64_t{p.w} * r.h > int64_t{p.h} * r.w) {
    src_w = std::max(1, static_cast<int>(int64_t{p.h} * r.w / r.h));
  } else {
    src_h = std::max(1, static_cast<int>(int64_t{p.w} * r.h / r.w));
  }
  const int src_x = (p.w - src_w) / 2;
  const int src_y = (p.h - src_h) / 2;

  // Atkinson dithering: it keeps light areas clean and edges crisp, which
  // suits e-ink. Errors go to the next two rows, kept in three row buffers.
  constexpr int kMaxW = 512;
  const int w = std::min(r.w, kMaxW);
  int16_t rows[3][kMaxW + 4] = {};
  for (int y = 0; y < r.h; ++y) {
    int16_t* cur = rows[y % 3];
    int16_t* next = rows[(y + 1) % 3];
    int16_t* after = rows[(y + 2) % 3];
    const int sy0 = src_y + static_cast<int>(int64_t{y} * src_h / r.h);
    const int sy1 = std::max(sy0 + 1, src_y + static_cast<int>(int64_t{y + 1} * src_h / r.h));
    for (int x = 0; x < w; ++x) {
      // Average the source pixels this one covers.
      const int sx0 = src_x + static_cast<int>(int64_t{x} * src_w / r.w);
      const int sx1 = std::max(sx0 + 1, src_x + static_cast<int>(int64_t{x + 1} * src_w / r.w));
      uint32_t sum = 0;
      for (int sy = sy0; sy < sy1; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) sum += p.px[sy * p.w + sx];
      }
      const int v = static_cast<int>(sum / static_cast<uint32_t>((sy1 - sy0) * (sx1 - sx0))) + cur[x + 2];
      const bool white = v >= 128;
      fb.set_pixel(r.x + x, r.y + y, white ? Color::White : Color::Black);
      const int16_t e = static_cast<int16_t>((v - (white ? 255 : 0)) / 8);
      cur[x + 3] += e;
      cur[x + 4] += e;
      next[x + 1] += e;
      next[x + 2] += e;
      next[x + 3] += e;
      after[x + 2] += e;
    }
    std::fill_n(cur, kMaxW + 4, int16_t{0});  // becomes the row after next
  }
}

void draw_cover(Framebuffer& fb, Rect r, std::string_view title, std::string_view author, const CoverImage* picture) {
  if (picture != nullptr && picture->valid()) {
    draw_picture(fb, r, *picture);
    fb.draw_rect(r, Color::Black, 1);  // a hairline keeps light covers apart from the page
    return;
  }
  const int style = static_cast<int>(hash(title) % 3);  // 0 framed, 1 ink, 2 banded
  const bool ink = style == 1;
  const Color bg = ink ? Color::Black : Color::White;
  const Color fg = ink ? Color::White : Color::Black;
  const bool large = r.w >= 100;
  const bool page = r.w >= 400;  // the whole screen (sleep): bigger type, wider margins
  const int inset = page ? 18 : large ? 7 : 4;
  const Rect inner{r.x + inset, r.y + inset, r.w - 2 * inset, r.h - 2 * inset};

  fb.fill_rect(r, bg);
  if (!ink) fb.draw_rect(r, Color::Black, large ? 2 : 1);
  fb.draw_rect(inner, fg);
  const int band = style == 2 ? inner.h / 7 : 0;
  if (band) fb.fill_rect({inner.x, inner.y, inner.w, band}, Color::Black);

  if (!large) {
    const Font& font = italic_font();
    const uint32_t cp = initial(title);
    const Glyph& g = font.glyphs[font.find(cp)];
    char letter[5] = {};
    size_t len = 0;
    if (cp < 0x80) {
      letter[len++] = static_cast<char>(cp);
    } else if (cp < 0x800) {
      letter[len++] = static_cast<char>(0xC0 | (cp >> 6));
      letter[len++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      letter[len++] = static_cast<char>(0xE0 | (cp >> 12));
      letter[len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      letter[len++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    const std::string_view s(letter, len);
    const int cy = inner.y + band + (inner.h - band) / 2;
    draw_text(fb, font, (r.x + (r.w - text_width_px(font, s)) / 2) * 64, cy + g.top / 2, s, fg);
    return;
  }

  const int text_x = inner.x + 4;
  const int text_w = inner.w - 8;
  const Font& cover_title = page ? screen_title_font() : small_italic_font();
  const int after = draw_wrapped(fb, cover_title, text_x, inner.y + band + inner.h / 8 + cover_title.ascent, text_w, 5,
                                 title, true, fg);
  const int rule = page ? 40 : 20;
  fb.hline(r.x + (r.w - rule) / 2, after - cover_title.line_height + (page ? 44 : 18), rule, fg);
  const Font& author_font = page ? text_font() : label_font();
  const int author_w = inner.w - 4;
  const Fit f = fit(author_font, author, author_w);
  draw_fit(fb, author_font, inner.x + 2 + (author_w - fit_width_px(author_font, author, f)) / 2,
           inner.y + inner.h - (page ? 48 : 14), author, f, fg);
}

void draw_book_row(Framebuffer& fb, Rect r, const LibraryEntry& book, const Strings& text, bool selected,
                   const CoverImage* picture) {
  const Color fg = selected ? Color::White : Color::Black;
  if (selected) fb.fill_round_rect(r, 10, Color::Black);

  const Rect cover{r.x + 12, r.y + (r.h - 60) / 2, 42, 60};
  if (selected) fb.fill_rect({cover.x - 2, cover.y - 2, cover.w + 4, cover.h + 4}, Color::White);
  draw_cover(fb, cover, book.title, book.author, picture);

  const BookState& state = book.state;
  char status[32];
  if (state.finished || (state.started() && state.percent >= 100)) {
    std::snprintf(status, sizeof(status), "%s", text.status_finished);
  } else if (!state.started()) {
    std::snprintf(status, sizeof(status), "%s", text.status_new);
  } else {
    std::snprintf(status, sizeof(status), "%d%%", state.percent);
  }
  const int text_x = cover.x + cover.w + 18;
  const int right = r.x + r.w - 14;
  const int status_w = text_width_px(label_font(), status);
  draw_right(fb, label_font(), right, r.y + 34, status, fg);
  draw_fitted(fb, text_font(), text_x, r.y + 34, right - text_x - status_w - 14, book.title, fg);
  draw_fitted(fb, label_font(), text_x, r.y + 58, right - text_x, book.author, fg);
}

bool draw_qr(Framebuffer& fb, int x, int y, int size, std::string_view text) {
  constexpr int kMaxVersion = 10;  // 57 modules: up to ~270 characters at low error correction
  constexpr int kQuiet = 2;        // white modules around the code; the screen's white adds more
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(kMaxVersion)];
  uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(kMaxVersion)];
  char buf[272];
  if (text.size() >= sizeof(buf)) return false;
  text.copy(buf, text.size());
  buf[text.size()] = '\0';
  if (!qrcodegen_encodeText(buf, temp, qr, qrcodegen_Ecc_LOW, 1, kMaxVersion, qrcodegen_Mask_AUTO, true)) return false;
  const int n = qrcodegen_getSize(qr);
  const int module = size / (n + 2 * kQuiet);  // whole pixels per module keep edges crisp
  if (module < 1) return false;
  const int origin_x = x + (size - n * module) / 2;
  const int origin_y = y + (size - n * module) / 2;
  fb.fill_rect({x, y, size, size}, Color::White);
  for (int my = 0; my < n; ++my) {
    for (int mx = 0; mx < n; ++mx) {
      if (qrcodegen_getModule(qr, mx, my)) {
        fb.fill_rect({origin_x + mx * module, origin_y + my * module, module, module}, Color::Black);
      }
    }
  }
  return true;
}

void set_menu_fonts(const MenuFonts& f) { g_menu = f; }
const MenuFonts& menu_fonts() { return g_menu; }

}  // namespace vellum::ui

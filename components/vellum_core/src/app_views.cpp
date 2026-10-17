// Library views: the cover grid (Settings › Home and library view), the
// folder browser (the library's Folders tab), the picture viewer, and
// moving finished books to books/Read/ (Settings › Move finished books).
//
// The grid shows each book as its small cover thumbnail at its own size,
// the title under it and a bar for how far it is read; the side buttons
// move through the tiles in reading order, so nothing else changes about
// how home and the library are used.
//
// The folder browser lists one folder of the card at a time: a row up,
// its folders, the books in it that the library knows, and its pictures
// (JPEG, PNG, BMP), which open full screen. It reaches as deep as the
// library's scan (Library::kMaxFolderDepth).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/app.h"
#include "vellum/covers.h"
#include "vellum/text.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

constexpr int kHeroLabelBase = 98;  // as on the list home screen
constexpr int kGridTop = 112;
constexpr int kTileH = 160;  // a cover, its title and its bar
constexpr int kTitleBase = 112;
constexpr int kSubtitleBase = 142;
constexpr int kListTop = 160;
constexpr int kHintRoom = 56;
constexpr int kPad = 12;

int wrap(int i, int count) { return count <= 0 ? 0 : (i % count + count) % count; }

Rect row_rect(const Framebuffer& fb, int y, int h) { return {kSide - kPad, y, fb.width() - 2 * (kSide - kPad), h}; }

Color highlight(Framebuffer& fb, Rect r, bool selected) {
  if (selected) fb.fill_round_rect(r, 10, Color::Black);
  return selected ? Color::White : Color::Black;
}

void dotted_rule(Framebuffer& fb, int y) {
  for (int x = kSide; x < fb.width() - kSide; x += 4) fb.set_pixel(x, y, Color::Black);
}

// Grid slot `slot` (left to right, then down) of a grid of `cols` starting at y = top.
Rect tile_rect(const Framebuffer& fb, int cols, int top, int slot) {
  const int w = (fb.width() - 2 * (kSide - kPad)) / cols;
  return {kSide - kPad + slot % cols * w, top + slot / cols * kTileH, w, kTileH};
}

// The store as a tile: a dotted frame where a cover would be, a plus and its name.
void draw_store_tile(Framebuffer& fb, Rect r, std::string_view label, bool selected) {
  const Color fg = highlight(fb, r, selected);
  const Rect box{r.x + (r.w - covers::kSmallW) / 2, r.y + 6, covers::kSmallW, covers::kSmallH};
  for (int x = box.x; x < box.x + box.w; x += 4) {
    fb.fill_rect({x, box.y, 2, 2}, fg);
    fb.fill_rect({x, box.y + box.h - 2, 2, 2}, fg);
  }
  for (int y = box.y; y < box.y + box.h; y += 4) {
    fb.fill_rect({box.x, y, 2, 2}, fg);
    fb.fill_rect({box.x + box.w - 2, y, 2, 2}, fg);
  }
  const int cx = box.x + box.w / 2;
  fb.fill_rect({cx - 10, box.y + 30, 20, 3}, fg);
  fb.fill_rect({cx - 1, box.y + 21, 3, 21}, fg);
  ui::draw_wrapped(fb, ui::label_font(), box.x + 4, box.y + 66, box.w - 8, 3, label, true, fg);
}

// A folder in the 42 x 60 place of a list row's cover: a tab over a body.
void draw_folder_icon(Framebuffer& fb, Rect r, Color fg) {
  const Color paper = fg == Color::Black ? Color::White : Color::Black;
  fb.fill_round_rect({r.x, r.y + 13, 18, 8}, 3, fg);
  fb.fill_round_rect({r.x, r.y + 17, r.w, 30}, 3, fg);
  fb.fill_rect({r.x + 2, r.y + 21, r.w - 4, 24}, paper);
}

// A picture in the same place: a frame, a mountain and a sun.
void draw_picture_icon(Framebuffer& fb, Rect r, Color fg) {
  const Color paper = fg == Color::Black ? Color::White : Color::Black;
  fb.fill_rect({r.x, r.y + 12, r.w, 36}, fg);
  fb.fill_rect({r.x + 2, r.y + 14, r.w - 4, 32}, paper);
  fb.fill_round_rect({r.x + 27, r.y + 19, 7, 7}, 3, fg);
  for (int dy = 0; dy < 13; ++dy) fb.hline(r.x + 6 + dy, r.y + 43 - dy, 26 - 2 * dy, fg);
}

// File names in any case of ASCII letters.
bool name_less(const char* a, const char* b) {
  for (;; ++a, ++b) {
    const int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : static_cast<uint8_t>(*a);
    const int y = *b >= 'A' && *b <= 'Z' ? *b + 32 : static_cast<uint8_t>(*b);
    if (x != y || x == 0) return x < y;
  }
}

}  // namespace

// ---- Cover grid --------------------------------------------------------------

void App::draw_home_grid() {
  const int w = fb_.width();
  const Strings& t = text();
  ui::draw_label(fb_, kSide, kHeroLabelBase, t.recent_label);
  if (update_available()) {
    draw_update_notice();
  } else {
    char count[32];
    format_book_count(settings_.language, library_.count(), count, sizeof(count));
    ui::draw_right(fb_, ui::label_font(), w - kSide, kHeroLabelBase, count);
  }

  // The current book first, then the others most recently read first, as
  // home_activate() counts them.
  const int current = library_.current();
  int order[Library::kMaxBooks];
  const int n = library_.by_recent(order, Library::kMaxBooks);
  const int tiles = std::min(kHomeTiles, library_.count());
  int item = 0;
  for (int k = -1; k < n && item < tiles; ++k) {
    const int book = k < 0 ? current : order[k];
    if (k >= 0 && book == current) continue;
    const Rect r = tile_rect(fb_, kGridCols, kGridTop, item);
    ui::draw_book_tile(fb_, r, library_.entry(book), home_sel_ == item, cover_of(book, false));
    add_hit(r, item);
    ++item;
  }

  draw_home_buttons(kGridTop + kHomeTiles / kGridCols * kTileH + 6, tiles);
  ui::draw_hint(fb_, t.hint_home);
}

int App::library_grid_rows() const { return std::max(1, (fb_.height() - kHintRoom - library_list_top()) / kTileH); }

void App::draw_library_grid(int top, int bottom) {
  const Strings& t = text();
  const int per_page = library_rows();
  const int first = library_page_ * per_page;
  const int shown = library_view_count_ + (library_has_store() ? 1 : 0);
  const int last = std::min(shown, first + per_page);
  const int first_book = library_first_book();
  for (int k = first; k < last; ++k) {
    // With no books, the store comes under the message that says so.
    const int slot = library_view_count_ == 0 ? kGridCols : k - first;
    const Rect r = tile_rect(fb_, kGridCols, top, slot);
    if (r.y + r.h > bottom) break;  // whole tiles above the keyboard
    const int item = first_book + k;
    const bool selected = item == library_sel_;
    if (k < library_view_count_) {
      const int book = library_view_[k];
      ui::draw_book_tile(fb_, r, library_.entry(book), selected, cover_of(book, false));
    } else {
      char label[160];
      if (search_[0] != '\0') {
        std::snprintf(label, sizeof(label), t.store_search_for, search_);
      } else {
        std::snprintf(label, sizeof(label), "%s", t.store_link);
      }
      draw_store_tile(fb_, r, label, selected);
    }
    add_hit(r, item);
  }
}

// ---- Folders -----------------------------------------------------------------

int App::folder_rows() const { return std::max(1, (fb_.height() - kListTop - kHintRoom) / ui::kRowHeight); }

void App::folder_item_path(int item, char* out, size_t size) const {
  // list_folder() keeps only items whose path fits.
  const char* name = folder_items_[item].name;
  const int n = folder_path_[0] == '\0' ? std::snprintf(out, size, "%s", name)
                                        : std::snprintf(out, size, "%s/%s", folder_path_, name);
  if (n < 0 || static_cast<size_t>(n) >= size) out[0] = '\0';
}

void App::open_folder(const char* path, const char* select) {
  char want[sizeof(folder_path_)];
  std::snprintf(want, sizeof(want), "%s", select);  // it may be an item about to be replaced
  if (path != folder_path_) std::snprintf(folder_path_, sizeof(folder_path_), "%s", path);
  list_folder();
  // The item asked for, else the first one after the way up.
  folder_sel_ = folder_count_ > 1 && folder_items_[0].kind == FolderKind::Up ? 1 : 0;
  for (int i = 0; want[0] != '\0' && i < folder_count_; ++i) {
    if (folder_items_[i].kind != FolderKind::Up && std::strcmp(folder_items_[i].name, want) == 0) {
      folder_sel_ = i;
      break;
    }
  }
  show(Screen::Folders);
}

void App::list_folder() {
  folder_count_ = 0;
  if (folder_items_ == nullptr) folder_items_ = new (std::nothrow) FolderItem[kFolderItems];
  if (folder_items_ == nullptr || board_.storage == nullptr) return;
  if (folder_path_[0] != '\0') folder_items_[folder_count_++] = {FolderKind::Up, -1, {}};
  const int first = folder_count_;
  board_.storage->list(
      folder_path_[0] != '\0' ? folder_path_ : ".",
      [](void* p, const char* name, bool is_dir, uint32_t) {
        App& app = *static_cast<App*>(p);
        if (app.folder_count_ >= kFolderItems || std::strlen(name) >= sizeof(FolderItem::name)) return;
        char path[sizeof(app.folder_path_)];
        const int len = app.folder_path_[0] != '\0' ? std::snprintf(path, sizeof(path), "%s/%s", app.folder_path_, name)
                                                    : std::snprintf(path, sizeof(path), "%s", name);
        if (len <= 0 || len >= static_cast<int>(sizeof(path))) return;
        FolderItem item{FolderKind::Folder, -1, {}};
        if (is_dir) {
          if (!Library::shown_folder(path)) return;  // too deep, hidden or Vellum's own
        } else if (is_picture_file(name)) {
          item.kind = FolderKind::Picture;
        } else if (is_book_file(name) && (item.book = static_cast<int16_t>(app.library_.find(path))) >= 0) {
          item.kind = FolderKind::Book;  // only books the library could read
        } else {
          return;
        }
        std::snprintf(item.name, sizeof(item.name), "%s", name);
        app.folder_items_[app.folder_count_++] = item;
      },
      this);
  // Folders by name, then books in library (title) order, then pictures by name.
  std::sort(folder_items_ + first, folder_items_ + folder_count_, [](const FolderItem& a, const FolderItem& b) {
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.kind == FolderKind::Book) return a.book < b.book;
    return name_less(a.name, b.name);
  });
}

void App::folder_up() {
  if (folder_path_[0] == '\0') {
    // Back to the library, on the Folders tab.
    build_library_view();
    library_sel_ = 2 + 3;
    library_show_tab(3);
    show(Screen::Library);
    return;
  }
  char* slash = std::strrchr(folder_path_, '/');
  char left[sizeof(folder_path_)];
  std::snprintf(left, sizeof(left), "%s", slash != nullptr ? slash + 1 : folder_path_);
  if (slash != nullptr) {
    *slash = '\0';
  } else {
    folder_path_[0] = '\0';
  }
  open_folder(folder_path_, left);  // on the folder just left
}

void App::folder_activate() {
  if (folder_sel_ < 0 || folder_sel_ >= folder_count_) return;
  const FolderItem& item = folder_items_[folder_sel_];
  switch (item.kind) {
    case FolderKind::Up:
      return folder_up();
    case FolderKind::Folder: {
      char path[sizeof(folder_path_)];
      folder_item_path(folder_sel_, path, sizeof(path));
      return open_folder(path);
    }
    case FolderKind::Book:
      return open_book(item.book);
    case FolderKind::Picture:
      return open_picture(folder_sel_);
  }
}

bool App::folder_key(Key key) {
  if (key == Key::Power) return folder_activate(), false;
  folder_sel_ = wrap(folder_sel_ + (key == Key::Next ? 1 : -1), folder_count_);
  return true;
}

bool App::folder_gesture(const Gesture& g) {
  using K = Gesture::Kind;
  if (g.kind == K::Tap || g.kind == K::LongPress) {
    const int item = hit_at(g.x, g.y);
    if (item < 0) return false;
    folder_sel_ = item;
    folder_activate();
    return false;
  }
  // Swipes page through the rows, landing on the first one of the page.
  const int rows = folder_rows();
  const int pages = (folder_count_ + rows - 1) / rows;
  if (pages <= 1) return false;
  const int forward = g.kind == K::SwipeLeft || g.kind == K::SwipeUp ? 1 : -1;
  folder_sel_ = wrap(folder_sel_ / rows + forward, pages) * rows;
  return true;
}

void App::draw_folders() {
  const int w = fb_.width();
  const Strings& t = text();
  draw_text(fb_, ui::screen_title_font(), kSide * 64, kTitleBase, t.folders);
  const int rows = folder_rows();
  const int pages = std::max(1, (folder_count_ + rows - 1) / rows);
  const int page = folder_sel_ / rows;
  if (pages > 1) {
    char label[32];
    std::snprintf(label, sizeof(label), "%d/%d", page + 1, pages);
    ui::draw_right(fb_, ui::label_font(), w - kSide, kTitleBase, label);
  }
  // Where this is: "books › Tolkien", or the card itself.
  char where[2 * sizeof(folder_path_)];
  size_t n = 0;
  for (const char* p = folder_path_; *p && n + 6 < sizeof(where); ++p) {
    if (*p == '/') {
      n += static_cast<size_t>(std::snprintf(where + n, sizeof(where) - n, " \xE2\x80\xBA "));  // ›
    } else {
      where[n++] = *p;
    }
  }
  where[n] = '\0';
  ui::draw_fitted(fb_, ui::label_font(), kSide, kSubtitleBase, w - 2 * kSide, n > 0 ? where : t.card_root);

  const int first = page * rows;
  const int last = std::min(folder_count_, first + rows);
  for (int i = first; i < last; ++i) {
    const FolderItem& item = folder_items_[i];
    const int y = kListTop + (i - first) * ui::kRowHeight;
    const Rect r = row_rect(fb_, y, ui::kRowHeight);
    const bool selected = i == folder_sel_;
    if (i > first && !selected && folder_sel_ != i - 1) dotted_rule(fb_, y);
    add_hit(r, i);
    if (item.kind == FolderKind::Book) {
      ui::draw_book_row(fb_, r, library_.entry(item.book), t, selected, cover_of(item.book, false));
      continue;
    }
    const Color fg = highlight(fb_, r, selected);
    const Rect icon{r.x + 12, r.y + (r.h - 60) / 2, 42, 60};
    const int text_x = icon.x + icon.w + 18;
    const int base = r.y + r.h / 2 + 8;
    const int right = r.x + r.w - kPad;
    if (item.kind == FolderKind::Up) {
      ui::draw_right(fb_, ui::text_font(), icon.x + 30, base, "\xE2\x80\xB9", fg);  // ‹
      ui::draw_fitted(fb_, ui::text_font(), text_x, base, right - text_x, t.folder_up, fg);
      continue;
    }
    if (item.kind == FolderKind::Folder) {
      draw_folder_icon(fb_, icon, fg);
      ui::draw_right(fb_, ui::text_font(), right, base, "\xE2\x80\xBA", fg);  // ›
    } else {
      draw_picture_icon(fb_, icon, fg);
    }
    ui::draw_fitted(fb_, ui::text_font(), text_x, base, right - 24 - text_x, item.name, fg);
  }
  const bool empty = folder_count_ == 0 || (folder_count_ == 1 && folder_items_[0].kind == FolderKind::Up);
  if (empty) {
    const int y = kListTop + folder_count_ * ui::kRowHeight + 40;
    ui::draw_wrapped(fb_, ui::text_font(), kSide, y, w - 2 * kSide, 3, t.folder_empty, false);
  }
  ui::draw_hint(fb_, t.hint_list);
}

// ---- Pictures ----------------------------------------------------------------

void App::open_picture(int item) {
  picture_item_ = item;
  folder_sel_ = item;
  picture_ready_ = false;
  show(Screen::Picture);  // a full refresh: no trace of the last picture
}

bool App::picture_step(int step) {
  if (step == 0) return false;
  for (int k = 1; k < folder_count_; ++k) {
    const int i = wrap(picture_item_ + step * k, folder_count_);
    if (folder_items_[i].kind == FolderKind::Picture) {
      open_picture(i);
      return false;
    }
  }
  return false;  // the only picture here
}

void App::draw_picture_screen() {
  const int w = fb_.width();
  const int h = fb_.height();
  fb_.clear(Color::White);
  if (!picture_ready_) {
    // Decoded once into the large cover buffer, until a cover needs it.
    char path[sizeof(folder_path_)];
    folder_item_path(picture_item_, path, sizeof(path));
    picture_ok_ = board_.storage != nullptr && large_cover_.px != nullptr &&
                  covers::load_picture(*board_.storage, path, large_cover_);
    picture_ready_ = true;
  }
  if (picture_ok_) {
    // As large as fits, keeping its shape, in the middle.
    const CoverImage& p = large_cover_;
    int pw = w, ph = h;
    if (int64_t{p.w} * h > int64_t{p.h} * w) {
      ph = std::max(1, static_cast<int>(int64_t{p.h} * w / p.w));
    } else {
      pw = std::max(1, static_cast<int>(int64_t{p.w} * h / p.h));
    }
    ui::draw_picture(fb_, {(w - pw) / 2, (h - ph) / 2, pw, ph}, p);
    return;
  }
  const Strings& t = text();
  ui::draw_centered(fb_, ui::text_font(), h / 2 - 16, t.picture_failed);
  ui::draw_wrapped(fb_, ui::label_font(), kSide, h / 2 + 20, w - 2 * kSide, 2, folder_items_[picture_item_].name, true);
  ui::draw_hint(fb_, t.hint_picture);
}

// ---- Finished books ------------------------------------------------------------

void App::book_finished(int book) {
  if (!settings_.move_finished || library_.entry(book).builtin != nullptr) return;
  for (int k = 0; k < finished_count_; ++k) {
    if (finished_queue_[k] == book) return;
  }
  if (finished_count_ < kFinishedQueue) finished_queue_[finished_count_++] = book;
}

void App::move_finished_books() {
  const int n = finished_count_;
  finished_count_ = 0;
  if (board_.storage == nullptr || !settings_.move_finished) return;
  Storage& storage = *board_.storage;
  const size_t read_len = std::strlen(Library::kReadFolder);
  bool moved = false;
  for (int k = 0; k < n; ++k) {
    const int book = finished_queue_[k];
    char from[sizeof(LibraryEntry::path)];
    std::snprintf(from, sizeof(from), "%s", library_.entry(book).path);
    // Still finished (it may have been unmarked), and not in Read/ yet.
    if (!library_.state(book).finished) continue;
    if (std::strncmp(from, Library::kReadFolder, read_len) == 0 && from[read_len] == '/') continue;
    // Its file is about to move: a copy held open is closed first.
    if (std::strcmp(epub_path_, from) == 0) {
      epub_.close();
      epub_path_[0] = '\0';
    }
    if (std::strcmp(text_path_, from) == 0) {
      text_book_.close();
      text_path_[0] = '\0';
    }
    if (!library_.move_book(storage, book, Library::kReadFolder)) continue;
    // Everything else that names the book by its path follows it.
    const char* to = library_.entry(book).path;
    shelves_.rename_book(from, to);
    highlights_.rename_book(from, to);
    stats_.rename_book(from, to);
    moved = true;
  }
  if (!moved) return;
  save_shelves();
  highlights_.save(storage);
  stats_.save(storage);
}

}  // namespace vellum

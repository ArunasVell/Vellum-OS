// The free book store: catalogues of public-domain books on the web, read
// over the home Wi-Fi. Sources (Project Gutenberg, Standard Ebooks) lead to
// their lists, a list to pages of books, a book to its page with a summary
// and a Download button. A downloaded book goes into books/ like any other
// and can be opened at once.
//
// Every fetch blocks: the screen says "Loading…" (or shows the download's
// progress) first. The radio stays on while the store is open and goes off
// when it closes.
//
// Search: Gutenberg's lists page starts with Search, which opens the
// on-screen keyboard; store_search() then shows the results like a list.
// A library search can carry its words here too (the library's last row).
//
// The reader's own catalogues (from the phone page) follow the built-in
// ones. Choosing one shows its first page, whose sections lead to more
// lists (Home goes back up) and whose search, when it has one, comes first.
// Their requests carry the catalogue's user and password.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "vellum/app.h"
#include "vellum/catalogues.h"
#include "vellum/store.h"
#include "vellum/text.h"
#include "vellum/textfile.h"
#include "vellum/ui.h"

namespace vellum {
namespace {

using ui::kSide;

constexpr int kTitleBase = 112;
constexpr int kSubtitleBase = 142;
constexpr int kListTop = 160;
constexpr int kSourceHeight = 84;  // a catalogue: its name and a line about it
constexpr int kLinkHeight = 50;    // a list, or a button
constexpr int kBookHeight = 72;    // a book in a list: title and author
constexpr int kBookRows = 7;       // rows on one screen of a list page
constexpr int kPad = 12;
constexpr int kHintRoom = 64;  // under the last button

int wrap(int i, int count) { return count <= 0 ? 0 : (i % count + count) % count; }

Rect row_rect(const Framebuffer& fb, int y, int h) { return {kSide - kPad, y, fb.width() - 2 * (kSide - kPad), h}; }

Color highlight(Framebuffer& fb, Rect r, bool selected) {
  if (selected) fb.fill_round_rect(r, 10, Color::Black);
  return selected ? Color::White : Color::Black;
}

void dotted_rule(Framebuffer& fb, int y) {
  for (int x = kSide; x < fb.width() - kSide; x += 4) fb.set_pixel(x, y, Color::Black);
}

// A row that leads somewhere: its name, and a › at the right.
void draw_link(Framebuffer& fb, Rect r, std::string_view text, bool selected) {
  const Color c = highlight(fb, r, selected);
  const int base = r.y + r.h / 2 + 8;
  ui::draw_fitted(fb, ui::text_font(), r.x + kPad, base, r.w - 2 * kPad - 24, text, c);
  ui::draw_right(fb, ui::text_font(), r.x + r.w - kPad, base, "\xE2\x80\xBA", c);  // ›
}

}  // namespace

// ---- Opening and leaving --------------------------------------------------------

void App::open_store() {
  if (board_.network == nullptr) return;
  if (store_ == nullptr) {
    store_ = new (std::nothrow) StoreMemory;  // large: in PSRAM on the device
    if (store_ == nullptr) return;
  }
  store_feed_.entries = store_->entries;
  store_feed_.capacity = kStoreEntries;
  store_feed_.count = 0;
  store_->catalogues = {};
  if (board_.storage != nullptr) store_->catalogues.load(*board_.storage);  // the phone page may have changed them
  store_custom_count_ = store_->catalogues.count();
  store_->auth[0] = '\0';
  store_->search[0] = '\0';
  store_page_ = store_source_count() > 1 ? StorePage::Sources : StorePage::Lists;
  store_source_ = 0;
  store_sel_ = 0;
  store_state_ = StoreState::Ready;
  store_error_ = StoreError::None;
  store_saved_ = -1;
  show(Screen::Store);
}

void App::close_store() {
  if (board_.network != nullptr) board_.network->disconnect();
}

void App::store_back() {
  if (store_busy()) return;
  store_state_ = StoreState::Ready;
  store_error_ = StoreError::None;
  switch (store_page_) {
    case StorePage::Book:
      // Back to the list, on the book's row.
      store_page_ = StorePage::Books;
      store_sel_ = store_book_ + store_lead_rows();
      if (store_feed_.count == 0) store_load(store_url_);  // the list did not load before
      break;
    case StorePage::Books:
      if (store_custom()) {
        // Up a section (or back a page), then out to the catalogues.
        if (store_depth_ > 0) {
          --store_depth_;
          char url[256];
          textfile::copy(store_->history[store_depth_], url, sizeof(url));
          textfile::copy(store_->history_titles[store_depth_], store_title_, sizeof(store_title_));
          return store_load(url);
        }
        store_page_ = StorePage::Sources;
        store_sel_ = store_source_;
        break;
      }
      store_page_ = StorePage::Lists;
      store_sel_ = 0;
      break;
    case StorePage::Lists:
      if (store_source_count() > 1) {
        store_page_ = StorePage::Sources;
        store_sel_ = store_source_;
        break;
      }
      [[fallthrough]];
    case StorePage::Sources:
      close_store();
      return show(Screen::Library);
  }
  draw();
  queue_.submit_full();
}

bool App::store_busy() const { return store_state_ == StoreState::Loading || store_state_ == StoreState::Downloading; }

int App::store_source_count() const { return store::source_count() + store_custom_count_; }

bool App::store_custom() const { return store_source_ >= store::source_count(); }

const char* App::store_source_name(int source) const {
  if (source < store::source_count()) return store::source(source).name;
  return store_->catalogues.at(source - store::source_count()).name;
}

int App::store_lead_rows() const {
  if (store_custom()) return store_depth_ == 0 && store_->search[0] != '\0' ? 1 : 0;
  return store_depth_ > 0 ? 1 : 0;
}

int App::store_rows_per_page() const {
  if (store_page_ == StorePage::Sources) return std::max(1, (fb_.height() - kHintRoom - kListTop - 8) / kSourceHeight);
  return kBookRows;
}

void App::store_push_history() {
  if (store_depth_ == kStoreHistory) {
    std::memmove(store_->history[0], store_->history[1], sizeof(store_->history[0]) * (kStoreHistory - 1));
    std::memmove(store_->history_titles[0], store_->history_titles[1],
                 sizeof(store_->history_titles[0]) * (kStoreHistory - 1));
    --store_depth_;
  }
  textfile::copy(store_url_, store_->history[store_depth_], sizeof(store_->history[0]));
  textfile::copy(store_title_, store_->history_titles[store_depth_], sizeof(store_->history_titles[0]));
  ++store_depth_;
}

// ---- Input ------------------------------------------------------------------------

bool App::store_key(Key key) {
  if (store_busy()) return false;
  if (key == Key::Power) {
    store_activate();
    return false;
  }
  const int count = store_row_count();
  if (count == 0) return false;
  store_sel_ = wrap(store_sel_ + (key == Key::Next ? 1 : -1), count);
  return true;
}

bool App::store_swipe(int forward) {
  if (store_busy() || (store_page_ != StorePage::Books && store_page_ != StorePage::Sources)) return false;
  const int count = store_row_count();
  const int per_page = store_rows_per_page();
  const int pages = (count + per_page - 1) / per_page;
  if (pages <= 1) return false;
  const int page = wrap(store_sel_ / per_page + forward, pages);
  store_sel_ = std::min(page * per_page, count - 1);
  return true;
}

int App::store_row_count() const {
  if (store_busy()) return 0;
  if (store_state_ == StoreState::Failed) return 1;  // Try again
  switch (store_page_) {
    case StorePage::Sources:
      return store_source_count();
    case StorePage::Lists:
      return (store::source(store_source_).search ? 1 : 0) + store::source(store_source_).list_count;
    case StorePage::Books:
      return store_lead_rows() + store_feed_.count + (store_feed_.next[0] != '\0' ? 1 : 0);
    case StorePage::Book:
      if (store_state_ == StoreState::Saved) return 2;  // Read now, back to the list
      return store_->detail.epub[0] != '\0' ? 1 : 0;
  }
  return 0;
}

void App::store_activate() {
  if (store_busy() || store_row_count() == 0) return;
  if (store_state_ == StoreState::Failed) {
    // Try again.
    store_state_ = StoreState::Ready;
    store_error_ = StoreError::None;
    if (store_page_ == StorePage::Book && store_failed_download_) return store_download();
    if (store_page_ == StorePage::Book) return store_open_book(store_book_);
    if (store_page_ == StorePage::Books) return store_load(store_url_);
  }
  switch (store_page_) {
    case StorePage::Sources:
      store_source_ = store_sel_;
      if (store_custom()) {
        // One of the reader's own: straight to its first page.
        const Catalogue& c = store_->catalogues.at(store_source_ - store::source_count());
        if (!basic_auth_header(c, store_->auth, sizeof(store_->auth))) store_->auth[0] = '\0';
        store_->search[0] = '\0';
        textfile::copy(c.name, store_title_, sizeof(store_title_));
        store_depth_ = 0;
        char url[256];
        textfile::copy(c.url, url, sizeof(url));
        return store_load(url);
      }
      store_page_ = StorePage::Lists;
      store_sel_ = 0;
      draw();
      queue_.submit_full();
      return;
    case StorePage::Lists: {
      const store::Source& source = store::source(store_source_);
      if (source.search && store_sel_ == 0) {
        open_keyboard(Typing::StoreSearch, "");
        draw();
        queue_.submit();
        return;
      }
      const store::List& list = source.lists[store_sel_ - (source.search ? 1 : 0)];
      textfile::copy(text().store_lists[list.name], store_title_, sizeof(store_title_));
      store_depth_ = 0;
      return store_load(list.url);
    }
    case StorePage::Books: {
      int row = store_sel_;
      if (store_lead_rows() > 0 && row-- == 0) {
        if (store_custom()) {
          // The catalogue's own search.
          open_keyboard(Typing::StoreSearch, "");
          draw();
          queue_.submit();
          return;
        }
        // Earlier books: the page before this one.
        char url[256];
        textfile::copy(store_->history[--store_depth_], url, sizeof(url));
        return store_load(url);
      }
      if (row == store_feed_.count) {
        // More books: remember this page, then the next.
        store_push_history();
        char url[256];
        textfile::copy(store_feed_.next, url, sizeof(url));
        return store_load(url);
      }
      if (store_->entries[row].section) {
        // A section of the catalogue: its list, Home coming back here.
        store_push_history();
        textfile::copy(store_->entries[row].title, store_title_, sizeof(store_title_));
        char url[256];
        textfile::copy(store_->entries[row].detail, url, sizeof(url));
        return store_load(url);
      }
      return store_open_book(row);
    }
    case StorePage::Book:
      if (store_state_ == StoreState::Saved) {
        if (store_sel_ == 0 && store_saved_ >= 0) {
          close_store();
          return open_book(store_saved_);
        }
        return store_back();
      }
      return store_download();
  }
}

// ---- Fetching ---------------------------------------------------------------------

void App::store_fail(StoreError e) {
  store_state_ = StoreState::Failed;
  store_error_ = e;
  store_sel_ = 0;
}

App::StoreError App::net_error(NetResult r) {
  using E = StoreError;
  switch (r) {
    case NetResult::NoNetwork:
      return E::NoNetwork;
    case NetResult::NoWifi:
      return E::NoWifi;
    case NetResult::BrokeOff:
      return E::BrokeOff;
    default:
      return E::NoServer;
  }
}

const char* App::store_error_text() const {
  const Strings& t = text();
  switch (store_error_) {
    case StoreError::None:
      break;
    case StoreError::NoNetwork:
      return t.store_no_home_wifi;
    case StoreError::NoWifi:
      return t.wifi_failed;
    case StoreError::NoServer:
      return t.store_no_server;
    case StoreError::BrokeOff:
      return t.store_broke_off;
    case StoreError::NotBook:
      return t.store_not_book;
    case StoreError::CardFull:
      return t.card_full;
    case StoreError::NoCard:
      return t.no_card;
    case StoreError::NoEpub:
      return t.store_no_epub;
  }
  return "";
}

void App::store_load(const char* url) {
  textfile::copy(url, store_url_, sizeof(store_url_));
  store_page_ = StorePage::Books;
  store_state_ = StoreState::Loading;
  store_feed_.count = 0;
  store_feed_.next[0] = '\0';
  draw();
  queue_.submit();
  const bool custom = store_custom();
  const NetResult r =
      store::fetch_feed(*board_.network, store_url_, store_feed_, store_->parser, custom ? store_->auth : nullptr, custom);
  if (custom && store_depth_ == 0 && r == NetResult::Ok) {
    // The catalogue's search, from its first page.
    textfile::copy(store_->parser.search(), store_->search, sizeof(store_->search));
    store_->search_template = store_->parser.search_is_template();
  }
  store_state_ = StoreState::Ready;
  store_sel_ = store_feed_.count > 0 ? store_lead_rows() : 0;  // the first book
  if (r != NetResult::Ok) store_fail(net_error(r));
  draw();
  queue_.submit_full();
}

void App::store_search(const char* words) {
  char url[256];
  if (board_.network == nullptr || store_ == nullptr) return;
  if (store_custom() && store_page_ == StorePage::Books && store_->search[0] != '\0') {
    bool ok = false;
    if (store_->search_template) {
      ok = search_from_template(store_->search, words, url, sizeof(url));
    } else {
      // An OpenSearch description first: it holds the address to fill in.
      store_state_ = StoreState::Loading;
      draw();
      queue_.submit();
      struct Sink {
        char* buf;
        size_t len;
        size_t cap;
      } sink{store_->description, 0, sizeof(store_->description) - 1};
      const NetResult r = store::get(
          *board_.network, store_->search, store_->auth,
          [](void* ctx, const uint8_t* data, uint32_t len, uint32_t) {
            auto& s = *static_cast<Sink*>(ctx);
            const size_t take = std::min<size_t>(len, s.cap - s.len);
            std::memcpy(s.buf + s.len, data, take);
            s.len += take;
            return true;
          },
          &sink);
      store_state_ = StoreState::Ready;
      char tmpl[256];
      ok = r == NetResult::Ok &&
           opensearch_template(std::string_view(store_->description, sink.len), store_->search, tmpl, sizeof(tmpl)) &&
           search_from_template(tmpl, words, url, sizeof(url));
      if (ok) {
        // The next search needs no description.
        textfile::copy(tmpl, store_->search, sizeof(store_->search));
        store_->search_template = true;
      }
    }
    if (!ok) {
      store_fail(StoreError::NoServer);
      draw();
      queue_.submit_full();
      return;
    }
    store_push_history();  // Home comes back to the catalogue
    textfile::copy(words, store_title_, sizeof(store_title_));
    return store_load(url);
  }
  if (!store::search_url(words, url, sizeof(url))) return;
  textfile::copy(words, store_title_, sizeof(store_title_));
  store_source_ = 0;  // Project Gutenberg
  store_depth_ = 0;
  store_load(url);
}

void App::store_open_book(int i) {
  store_book_ = i;
  store_page_ = StorePage::Book;
  store_state_ = StoreState::Ready;
  store_failed_download_ = false;
  store_sel_ = 0;
  OpdsEntry& book = store_->detail;
  book = store_->entries[i];
  if (book.epub[0] == '\0' && book.detail[0] != '\0') {
    // Gutenberg lists only name the book: its own feed has the rest.
    store_state_ = StoreState::Loading;
    draw();
    queue_.submit_full();
    OpdsFeed feed;
    feed.entries = &book;
    feed.capacity = 1;
    char url[256];
    textfile::copy(book.detail, url, sizeof(url));
    const bool custom = store_custom();
    const NetResult r =
        store::fetch_feed(*board_.network, url, feed, store_->parser, custom ? store_->auth : nullptr, custom);
    store_state_ = StoreState::Ready;
    if (feed.count == 0) book = store_->entries[i];
    if (book.title[0] == '\0') textfile::copy(store_->entries[i].title, book.title, sizeof(book.title));
    if (book.author[0] == '\0') textfile::copy(store_->entries[i].author, book.author, sizeof(book.author));
    if (r != NetResult::Ok) store_fail(net_error(r));
    draw();
    queue_.submit();
    return;
  }
  draw();
  queue_.submit_full();
}

void App::store_download() {
  const OpdsEntry& book = store_->detail;
  store_failed_download_ = true;
  char path[160];
  if (board_.storage == nullptr) {
    store_fail(StoreError::NoCard);
  } else if (!store::book_path(*board_.storage, book.title, book.author, path, sizeof(path))) {
    store_fail(StoreError::CardFull);
  } else {
    store_state_ = StoreState::Downloading;
    store_percent_ = 0;
    draw();
    queue_.submit();
    const store::Saved r = store::download(
        *board_.network, *board_.storage, book.epub, path,
        [](void* ctx, int percent) {
          // Redraw every tenth: each refresh takes a moment of its own.
          auto* app = static_cast<App*>(ctx);
          if (percent / 10 == app->store_percent_ / 10) return;
          app->store_percent_ = percent;
          app->draw();
          app->queue_.submit();
        },
        this, store_custom() ? store_->auth : nullptr);
    switch (r) {
      case store::Saved::Ok:
        store_state_ = StoreState::Saved;
        store_sel_ = 0;
        rescan_library();
        store_saved_ = -1;
        for (int k = 0; k < library_.count(); ++k) {
          if (std::strcmp(library_.entry(k).path, path) == 0) store_saved_ = k;
        }
        break;
      case store::Saved::NoNetwork:
        store_fail(StoreError::NoNetwork);
        break;
      case store::Saved::NoWifi:
        store_fail(StoreError::NoWifi);
        break;
      case store::Saved::NotBook:
        store_fail(StoreError::NotBook);
        break;
      case store::Saved::CardFull:
        store_fail(StoreError::CardFull);
        break;
      case store::Saved::NoServer:
        store_fail(store_percent_ > 0 ? StoreError::BrokeOff : StoreError::NoServer);
        break;
    }
  }
  draw();
  queue_.submit();
}

// ---- Drawing ----------------------------------------------------------------------

void App::draw_store() {
  const int w = fb_.width();
  const Strings& t = text();
  const store::Source& source = store::source(store_custom() ? 0 : store_source_);  // Lists: a built-in one
  if (store_page_ == StorePage::Book) return draw_store_book();

  const char* title = t.store_title;
  const char* subtitle = board_.network->demo() ? t.store_demo : t.store_lead;
  if (store_page_ == StorePage::Lists) {
    title = source.name;
    if (!board_.network->demo()) subtitle = t.store_abouts[source.about];
  } else if (store_page_ == StorePage::Books) {
    title = store_title_;
    subtitle = store_source_name(store_source_);
  }
  ui::draw_fitted(fb_, ui::screen_title_font(), kSide, kTitleBase, w - 2 * kSide, title);
  ui::draw_fitted(fb_, ui::label_font(), kSide, kSubtitleBase, w - 2 * kSide, subtitle);

  // Loading, or what went wrong (with a way to try again).
  const char* message = nullptr;
  if (store_state_ == StoreState::Loading) message = t.store_loading;
  if (store_state_ == StoreState::Failed) message = store_error_text();
  if (store_page_ == StorePage::Books && store_state_ == StoreState::Ready && store_row_count() == 0) {
    message = t.store_empty;
  }
  if (message != nullptr) {
    const int y = ui::draw_wrapped(fb_, ui::text_font(), kSide, kListTop + 50, w - 2 * kSide, 4, message, false);
    if (store_state_ == StoreState::Failed) {
      const Rect r = row_rect(fb_, y + 10, kLinkHeight);
      draw_link(fb_, r, t.store_retry, true);
      add_hit(r, 0);
    }
    ui::draw_hint(fb_, store_busy() ? t.hint_store_busy : t.hint_list);
    return;
  }

  switch (store_page_) {
    case StorePage::Sources: {
      // The built-in catalogues, then the reader's own, a screen at a time.
      const int count = store_source_count();
      const int per_page = store_rows_per_page();
      const int first = store_sel_ / per_page * per_page;
      if (count > per_page) {
        char label[48];
        std::snprintf(label, sizeof(label), t.page_of, first / per_page + 1, (count + per_page - 1) / per_page);
        ui::draw_right(fb_, ui::label_font(), w - kSide, kSubtitleBase, label);
      }
      for (int i = first; i < std::min(count, first + per_page); ++i) {
        const Rect r = row_rect(fb_, kListTop + 8 + (i - first) * kSourceHeight, kSourceHeight);
        const bool selected = i == store_sel_;
        if (i > first && !selected && store_sel_ != i - 1) dotted_rule(fb_, r.y);
        const Color c = highlight(fb_, r, selected);
        const char* about = nullptr;
        if (i < store::source_count()) {
          about = t.store_abouts[store::source(i).about];
        } else {
          about = store_->catalogues.at(i - store::source_count()).url;  // its address, without the scheme
          if (const char* host = std::strstr(about, "://")) about = host + 3;
        }
        ui::draw_fitted(fb_, ui::large_font(), r.x + kPad, r.y + 38, r.w - 2 * kPad - 24, store_source_name(i), c);
        ui::draw_fitted(fb_, ui::label_font(), r.x + kPad, r.y + 66, r.w - 2 * kPad - 24, about, c);
        ui::draw_right(fb_, ui::text_font(), r.x + r.w - kPad, r.y + 38, "\xE2\x80\xBA", c);  // ›
        add_hit(r, i);
      }
      break;
    }
    case StorePage::Lists: {
      const int first_list = source.search ? 1 : 0;
      for (int i = 0; i < store_row_count(); ++i) {
        const Rect r = row_rect(fb_, kListTop + 8 + i * kLinkHeight, kLinkHeight);
        if (i > 0 && i != store_sel_ && store_sel_ != i - 1) dotted_rule(fb_, r.y);
        draw_link(fb_, r, i < first_list ? t.search : t.store_lists[source.lists[i - first_list].name],
                  i == store_sel_);
        add_hit(r, i);
      }
      break;
    }
    case StorePage::Books: {
      // A screen of rows: "Earlier books" first on a later page, the
      // books, "More books" last when the list goes on.
      const int count = store_row_count();
      const int pages = (count + kBookRows - 1) / kBookRows;
      const int first = store_sel_ / kBookRows * kBookRows;
      if (pages > 1) {
        char label[48];
        std::snprintf(label, sizeof(label), t.page_of, store_sel_ / kBookRows + 1, pages);
        ui::draw_right(fb_, ui::label_font(), w - kSide, kSubtitleBase, label);
      }
      for (int i = first; i < std::min(count, first + kBookRows); ++i) {
        const Rect r = row_rect(fb_, kListTop + (i - first) * kBookHeight, kBookHeight);
        const bool selected = i == store_sel_;
        if (i > first && !selected && store_sel_ != i - 1) dotted_rule(fb_, r.y);
        add_hit(r, i);
        const int book = i - store_lead_rows();
        if (book < 0 || book >= store_feed_.count) {
          const char* label = book >= 0 ? t.store_more : store_custom() ? t.search : t.store_earlier;
          draw_link(fb_, {r.x, r.y + 11, r.w, kLinkHeight}, label, selected);
          continue;
        }
        const OpdsEntry& e = store_->entries[book];
        const Color c = highlight(fb_, r, selected);
        if (e.section) {
          // A section: its name, what it holds, and a ›.
          ui::draw_fitted(fb_, ui::text_font(), r.x + kPad, r.y + 32, r.w - 2 * kPad - 24, e.title, c);
          ui::draw_fitted(fb_, ui::small_italic_font(), r.x + kPad, r.y + 58, r.w - 2 * kPad - 24, e.summary, c);
          ui::draw_right(fb_, ui::text_font(), r.x + r.w - kPad, r.y + 32, "\xE2\x80\xBA", c);  // ›
          continue;
        }
        ui::draw_fitted(fb_, ui::text_font(), r.x + kPad, r.y + 32, r.w - 2 * kPad, e.title, c);
        ui::draw_fitted(fb_, ui::small_italic_font(), r.x + kPad, r.y + 58, r.w - 2 * kPad, e.author, c);
      }
      break;
    }
    case StorePage::Book:
      break;
  }
  ui::draw_hint(fb_, t.hint_list);
}

void App::draw_store_book() {
  const int w = fb_.width();
  const int h = fb_.height();
  const Strings& t = text();
  const OpdsEntry& book = store_->detail;

  // Title, author, and a line of facts: the catalogue, the size.
  int y = ui::draw_wrapped(fb_, ui::screen_title_font(), kSide, kTitleBase, w - 2 * kSide, 2, book.title, false);
  ui::draw_fitted(fb_, ui::large_font(), kSide, y - 4, w - 2 * kSide, book.author);
  y += 30;
  char facts[128];
  int len = std::snprintf(facts, sizeof(facts), "%s", store_source_name(store_source_));
  if (book.size > 0) {
    char size[24];
    store::format_size(settings_.language, book.size, size, sizeof(size));
    len += std::snprintf(facts + len, sizeof(facts) - len, " \xC2\xB7 EPUB \xC2\xB7 %s", size);  // ·
  }
  if (book.language[0] != '\0') std::snprintf(facts + len, sizeof(facts) - len, " \xC2\xB7 %s", book.language);
  ui::draw_label(fb_, kSide, y, facts);
  y += 14;
  dotted_rule(fb_, y);

  // What happens at the bottom: buttons, progress or a message.
  const int rows = store_row_count();
  const int bottom = h - kHintRoom - rows * kLinkHeight;
  const char* message = nullptr;
  switch (store_state_) {
    case StoreState::Loading:
      message = t.store_loading;
      break;
    case StoreState::Saved:
      message = t.store_saved;
      break;
    case StoreState::Failed:
      message = store_error_text();
      break;
    case StoreState::Ready:
      if (book.epub[0] == '\0') message = t.store_no_epub;
      break;
    case StoreState::Downloading:
      break;
  }
  const int line = ui::text_font().line_height;
  int message_lines = message != nullptr ? 3 : 0;
  if (store_state_ == StoreState::Downloading) message_lines = 2;
  const int summary_bottom = bottom - message_lines * line - 16;

  // The summary, as much as fits.
  const int lines = (summary_bottom - (y + 12)) / line;
  if (book.summary[0] != '\0' && lines > 0) {
    ui::draw_wrapped(fb_, ui::text_font(), kSide, y + 12 + line - 6, w - 2 * kSide, lines, book.summary, false);
  }

  int msg_y = summary_bottom + line;
  if (store_state_ == StoreState::Downloading) {
    char progress[48];
    std::snprintf(progress, sizeof(progress), t.store_downloading, store_percent_);
    draw_text(fb_, ui::text_font(), kSide * 64, msg_y, progress);
    ui::draw_progress(fb_, kSide, msg_y + 24, w - 2 * kSide, store_percent_, 100);
  } else if (message != nullptr) {
    ui::draw_wrapped(fb_, ui::text_font(), kSide, msg_y, w - 2 * kSide, 3, message, false);
  }

  const char* names[2] = {t.store_download, nullptr};
  if (store_state_ == StoreState::Saved) {
    names[0] = t.store_read_now;
    names[1] = t.store_back;
  } else if (store_state_ == StoreState::Failed) {
    names[0] = t.store_retry;
  }
  for (int i = 0; i < rows && i < 2; ++i) {
    const Rect r = row_rect(fb_, bottom + i * kLinkHeight, kLinkHeight);
    if (i > 0 && i != store_sel_ && store_sel_ != i - 1) dotted_rule(fb_, r.y);
    draw_link(fb_, r, names[i], i == store_sel_);
    add_hit(r, i);
  }
  ui::draw_hint(fb_, store_busy() ? t.hint_store_busy : t.hint_updates);
}

}  // namespace vellum

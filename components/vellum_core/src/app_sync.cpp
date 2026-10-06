// KOReader sync: the open book's position shared with a KOReader sync
// server (kosync.h), over the home Wi-Fi like the store.
//
// Contents › Sync progress first asks the server: a position further on,
// sent by another device, moves the book there; otherwise this one is
// sent. With "sync automatically" on (phone page), closing the book and
// sleeping send the position when it moved since the last send. Either
// way the radio goes off again at once.
#include <cstdio>
#include <cstring>

#include "vellum/app.h"

namespace vellum {

bool App::can_sync() const {
  return board_.network != nullptr && board_.storage != nullptr && sync_account_.ready() && reading_ >= 0 &&
         library_.entry(reading_).builtin == nullptr && reader_.document() != nullptr;
}

bool App::sync_document() {
  if (sync_doc_book_ != 0 && sync_doc_book_ == reader_.id()) return true;
  sync_doc_book_ = 0;
  if (!kosync::document_id(*board_.storage, library_.entry(reading_).path, sync_account_.method, sync_doc_)) {
    return false;
  }
  sync_doc_book_ = reader_.id();
  return true;
}

namespace {

// The share of the book before the page shown, in whole percent, as the server has it.
int share_percent(const Document& doc, const Reader& reader) {
  return static_cast<int>(kosync::share_at(doc, reader.chapter(), reader.page(), reader.page_count()) * 100 + 0.5);
}

// This reader as the server tells devices apart: its name made into an id.
void device_id(const Board& board, char out[33]) {
  const char* name = board.peers != nullptr ? board.peers->name() : board.name;
  kosync::md5_hex(name, std::strlen(name), out);
}

}  // namespace

// Registers first when the phone page asked to (a name already taken may
// be this user's own: the login then decides), then sends the page shown.
static kosync::Result send_position(Network& net, Storage& card, kosync::Account& account, const char* document,
                                    const Document& doc, const Reader& reader, const char* id, bool epub) {
  if (account.create) {
    const kosync::Result r = kosync::register_user(net, account);
    if (r != kosync::Result::Ok && r != kosync::Result::Taken) return r;
    account.create = false;
    kosync::save_account(card, account);
  }
  const double share = kosync::share_at(doc, reader.chapter(), reader.page(), reader.page_count());
  // KOReader moves by the xpointer: an EPUB's chapter start is one it takes.
  char where[64];
  if (epub) {
    std::snprintf(where, sizeof(where), "/body/DocFragment[%d]/body", reader.chapter() + 1);
  } else {
    std::snprintf(where, sizeof(where), "%.4f", share);
  }
  return kosync::push(net, account, document, share, where, id);
}

void App::sync_progress(bool pull) {
  if (!can_sync()) return;
  sync_state_ = SyncState::Busy;
  draw();
  queue_.submit();
  Network& net = *board_.network;
  const Document& doc = *reader_.document();
  const bool epub = reader_.document() == &epub_;
  char id[33];
  device_id(board_, id);
  kosync::Result r = sync_document() ? kosync::Result::Ok : kosync::Result::NoServer;
  bool send = true;
  if (r == kosync::Result::Ok && pull && !sync_account_.create) {
    kosync::Progress there;
    r = kosync::pull(net, sync_account_, sync_doc_, there);
    if (r == kosync::Result::None) r = kosync::Result::Ok;  // nothing yet: send this one
    if (r == kosync::Result::Ok && there.device_id[0] != '\0' && std::strcmp(there.device_id, id) != 0) {
      kosync::Spot spot;
      if (there.share >= 0) {
        spot = kosync::spot_at(doc, there.share);
      } else {
        spot.chapter = kosync::xpointer_chapter(there.where);
      }
      const int here_chapter = reader_.chapter();
      const int here_page = reader_.page();
      const bool same_chapter = spot.chapter == here_chapter;
      const int there_page = same_chapter ? kosync::page_at(spot.within, reader_.page_count()) : 0;
      if (spot.chapter >= 0 && spot.chapter < doc.chapter_count() &&
          (spot.chapter > here_chapter || (same_chapter && there_page > here_page))) {
        // Further on: go there, to the page that starts nearest.
        const BookPos before = reader_.position();
        const ReadingStyle style = reading_style(settings_, &book_fonts());
        Document& book = *reader_.document();
        if (reader_.open(book, reader_.id(), style, {static_cast<uint16_t>(spot.chapter), {}}, fb_)) {
          const int target = kosync::page_at(spot.within, reader_.page_count());
          while (reader_.page() < target && reader_.next()) {
          }
          BookState& state = library_.state(reading_);
          state.position = reader_.position();
          state.percent = static_cast<uint8_t>(reader_.percent());
          save_progress();
          sync_state_ = SyncState::Moved;
          sync_percent_ = share_percent(doc, reader_);
          sync_sent_ = reader_.position();  // the server has it
          sync_sent_book_ = reader_.id();
          net.disconnect();
          show(Screen::Reader);
          return;
        }
        reader_.open(book, reader_.id(), style, before, fb_);
      } else if (spot.chapter == here_chapter && there_page == here_page) {
        send = false;  // the same page already
        sync_state_ = SyncState::UpToDate;
      }
    }
  }
  if (r == kosync::Result::Ok && send) {
    r = send_position(net, *board_.storage, sync_account_, sync_doc_, doc, reader_, id, epub);
    if (r == kosync::Result::Ok) {
      sync_state_ = SyncState::Sent;
      sync_percent_ = share_percent(doc, reader_);
      sync_sent_ = reader_.position();
      sync_sent_book_ = reader_.id();
    }
  }
  net.disconnect();
  if (r != kosync::Result::Ok) {
    sync_state_ = SyncState::Failed;
    sync_error_ = r;
  }
  draw();
  queue_.submit();
}

void App::sync_auto() {
  if (!can_sync() || !sync_account_.automatic) return;
  const BookPos here = reader_.position();
  if (sync_sent_book_ == reader_.id() && sync_sent_.chapter == here.chapter && sync_sent_.text == here.text) return;
  if (!sync_document()) return;
  char id[33];
  device_id(board_, id);
  const bool epub = reader_.document() == &epub_;
  if (send_position(*board_.network, *board_.storage, sync_account_, sync_doc_, *reader_.document(), reader_, id,
                    epub) == kosync::Result::Ok) {
    sync_sent_ = here;
    sync_sent_book_ = reader_.id();
  }
  board_.network->disconnect();
}

}  // namespace vellum

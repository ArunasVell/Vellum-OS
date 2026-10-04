#include "vellum/peer_transfer.h"

#include <cstdio>
#include <cstring>

#include "vellum/upload_page.h"

namespace vellum {

uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
  // Nibble table: small, and fast enough next to the radio.
  static constexpr uint32_t kTable[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                          0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                          0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  const auto* p = static_cast<const uint8_t*>(data);
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    crc = (crc >> 4) ^ kTable[crc & 15];
    crc = (crc >> 4) ^ kTable[crc & 15];
  }
  return ~crc;
}

namespace {

// Every packet: 'V' 'L', version, type, then the session (little endian).
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeader = 8;
enum Type : uint8_t {
  kHello = 1,   // sender, to everyone: name
  kHere = 2,    // receiver: name
  kOffer = 3,   // sender: size (4), book file name, sender name
  kAccept = 4,  // receiver
  kRefuse = 5,  // receiver: reason (1)
  kData = 6,    // sender: piece number (4), bytes
  kAck = 7,     // receiver: next piece wanted (4)
  kDone = 8,    // sender: CRC-32 of the book (4)
  kResult = 9,  // receiver: result (1)
};
enum Reason : uint8_t { kBusy = 1, kNoSpace = 2, kBadName = 3 };
enum Result : uint8_t { kCorrupt = 0, kSaved = 1, kCardFull = 2 };

constexpr uint32_t kPiece = PeerPacket::kMaxSize - kHeader - 4;
constexpr int kLookTicks = 200;      // about a minute of asking
constexpr int kSenderPatience = 20;  // quiet ticks before giving up
constexpr int kReceiverPatience = 40;

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t get32(const uint8_t* p) {
  return uint32_t{p[0]} | uint32_t{p[1]} << 8 | uint32_t{p[2]} << 16 | uint32_t{p[3]} << 24;
}

uint32_t header(uint8_t* out, uint8_t type, uint32_t session) {
  out[0] = 'V';
  out[1] = 'L';
  out[2] = kVersion;
  out[3] = type;
  put32(out + 4, session);
  return kHeader;
}

bool parse(const PeerPacket& p, uint8_t& type, uint32_t& session) {
  if (p.size < kHeader || p.data[0] != 'V' || p.data[1] != 'L' || p.data[2] != kVersion) return false;
  type = p.data[3];
  session = get32(p.data + 4);
  return true;
}

// Copies a NUL-terminated string from the packet body at `at`, moving past it.
void take_string(const PeerPacket& p, uint32_t& at, char* out, size_t size) {
  size_t n = 0;
  while (at < p.size && p.data[at] != 0) {
    if (n + 1 < size) out[n++] = static_cast<char>(p.data[at]);
    ++at;
  }
  out[n] = '\0';
  ++at;
}

uint32_t put_string(uint8_t* out, uint32_t at, const char* s) {
  const size_t n = std::strlen(s);
  if (at + n + 1 > PeerPacket::kMaxSize) return at;
  std::memcpy(out + at, s, n + 1);
  return at + static_cast<uint32_t>(n) + 1;
}

// The packet being sent: kept so it can go again unchanged.
uint8_t g_out[PeerPacket::kMaxSize];
uint32_t g_out_size = 0;
PeerAddress g_out_to;

}  // namespace

// ---- Sender -----------------------------------------------------------------

bool PeerSender::begin(Peers& peers, Storage& storage, const char* path) {
  cancel();
  peers_ = &peers;
  storage_ = &storage;
  file_ = storage.open(path);
  if (file_ == nullptr) {
    state_ = State::Failed;
    error_ = Error::CannotRead;
    return false;
  }
  size_ = file_->size();
  const char* slash = std::strrchr(path, '/');
  std::snprintf(name_, sizeof(name_), "%s", slash != nullptr ? slash + 1 : path);
  static uint32_t counter = 0;
  session_ = crc32_update(++counter, name_, std::strlen(name_)) ^ size_;
  if (session_ == 0) session_ = 1;
  reader_count_ = 0;
  error_ = Error::None;
  state_ = State::Looking;
  quiet_ = 0;
  shown_percent_ = -1;
  tick();
  return true;
}

void PeerSender::cancel() {
  if (file_ != nullptr) storage_->close(file_);
  file_ = nullptr;
  state_ = State::Idle;
}

void PeerSender::fail(Error e) {
  if (file_ != nullptr) storage_->close(file_);
  file_ = nullptr;
  state_ = State::Failed;
  error_ = e;
}

void PeerSender::send_to(int i) {
  if (state_ != State::Looking || i < 0 || i >= reader_count_) return;
  target_ = i;
  g_out_to = readers_[i].address;
  uint32_t at = header(g_out, kOffer, session_);
  put32(g_out + at, size_);
  at = put_string(g_out, at + 4, name_);
  g_out_size = put_string(g_out, at, peers_->name());
  peers_->send(&g_out_to, g_out, g_out_size);
  state_ = State::Offering;
  quiet_ = 0;
}

void PeerSender::send_piece() {
  const uint32_t offset = seq_ * kPiece;
  if (offset >= size_) {
    // All there: ask the receiver to check and keep it.
    uint32_t at = header(g_out, kDone, session_);
    put32(g_out + at, crc_);
    g_out_size = at + 4;
    state_ = State::Finishing;
  } else {
    uint32_t at = header(g_out, kData, session_);
    put32(g_out + at, seq_);
    at += 4;
    const uint32_t want = size_ - offset < kPiece ? size_ - offset : kPiece;
    if (file_->read_at(offset, g_out + at, want) != static_cast<int32_t>(want)) return fail(Error::CannotRead);
    g_out_size = at + want;
  }
  peers_->send(&g_out_to, g_out, g_out_size);
}

bool PeerSender::on_packet(const PeerPacket& p) {
  uint8_t type;
  uint32_t session;
  if (!active() || !parse(p, type, session)) return false;

  if (type == kHere && state_ == State::Looking) {
    uint32_t at = kHeader;
    for (int i = 0; i < reader_count_; ++i) {
      if (readers_[i].address == p.from) return false;  // already listed
    }
    if (reader_count_ == peer::kMaxReaders) return false;
    Reader& r = readers_[reader_count_++];
    r.address = p.from;
    take_string(p, at, r.name, sizeof(r.name));
    if (r.name[0] == '\0') std::snprintf(r.name, sizeof(r.name), "Vellum");
    return true;
  }
  if (session != session_ || !(p.from == g_out_to)) return false;
  quiet_ = 0;
  switch (type) {
    case kAccept:
      if (state_ != State::Offering) return false;
      state_ = State::Sending;
      seq_ = 0;
      crc_ = 0;
      send_piece();
      return true;
    case kRefuse:
      if (state_ != State::Offering) return false;
      fail(p.size > kHeader && p.data[kHeader] == kNoSpace ? Error::CardFull : Error::Refused);
      return true;
    case kAck: {
      if (state_ != State::Sending || p.size < kHeader + 4 || get32(p.data + kHeader) != seq_ + 1) return false;
      crc_ = crc32_update(crc_, g_out + kHeader + 4, g_out_size - kHeader - 4);  // the piece now delivered
      ++seq_;
      send_piece();
      const int shown = percent() / 5;
      if (shown == shown_percent_ && state_ == State::Sending) return false;
      shown_percent_ = shown;
      return true;
    }
    case kResult:
      if (state_ != State::Finishing || p.size < kHeader + 1) return false;
      if (p.data[kHeader] == kSaved) {
        if (file_ != nullptr) storage_->close(file_);
        file_ = nullptr;
        state_ = State::Sent;
      } else {
        fail(p.data[kHeader] == kCardFull ? Error::CardFull : Error::Corrupt);
      }
      return true;
    default:
      return false;
  }
}

bool PeerSender::tick() {
  if (state_ == State::Looking) {
    if (quiet_ >= kLookTicks) {
      if (reader_count_ > 0) return false;  // keep the list; stop asking
      fail(Error::NoAnswer);
      return true;
    }
    ++quiet_;
    uint8_t hello[kHeader + peer::kNameSize];
    const uint32_t at = header(hello, kHello, 0);
    peers_->send(nullptr, hello, put_string(hello, at, peers_->name()));
    return false;
  }
  if (!active()) return false;
  if (++quiet_ > kSenderPatience) {
    fail(Error::NoAnswer);
    return true;
  }
  peers_->send(&g_out_to, g_out, g_out_size);  // lost on the way: again
  return false;
}

bool PeerSender::needs_ticks() const { return active() && !(state_ == State::Looking && quiet_ >= kLookTicks); }

int PeerSender::percent() const {
  if (state_ == State::Sent || state_ == State::Finishing) return 100;
  if (size_ == 0) return 0;
  const uint64_t done = uint64_t{seq_} * kPiece;
  return static_cast<int>((done > size_ ? size_ : done) * 100 / size_);
}

// ---- Receiver ---------------------------------------------------------------

void PeerReceiver::begin(Peers& peers, Storage& storage) {
  end();
  peers_ = &peers;
  storage_ = &storage;
  state_ = State::Listening;
  received_ = 0;
  card_full_ = false;
  last_[0] = '\0';
}

void PeerReceiver::end() {
  if (state_ == State::Receiving) abort();
  state_ = State::Off;
}

void PeerReceiver::abort() {
  if (writer_ != nullptr) storage_->finish(writer_, false);
  writer_ = nullptr;
  state_ = State::Listening;
}

void PeerReceiver::reply(uint8_t type, const void* body, uint32_t len) {
  uint8_t out[kHeader + 8];
  const uint32_t at = header(out, type, session_);
  if (len > 0) std::memcpy(out + at, body, len);
  peers_->send(&from_, out, at + len);
}

bool PeerReceiver::on_packet(const PeerPacket& p) {
  uint8_t type;
  uint32_t session;
  if (state_ == State::Off || !parse(p, type, session)) return false;

  switch (type) {
    case kHello: {
      if (state_ != State::Listening) return false;  // one book at a time
      uint8_t out[kHeader + peer::kNameSize];
      const uint32_t at = header(out, kHere, 0);
      peers_->send(&p.from, out, put_string(out, at, peers_->name()));
      return false;
    }
    case kOffer: {
      const bool same = session == session_ && p.from == from_;
      if (state_ == State::Receiving) {
        if (same) {
          reply(kAccept, nullptr, 0);  // the first answer was lost
          return false;
        }
        const PeerAddress keep = from_;
        const uint32_t keep_session = session_;
        from_ = p.from;
        session_ = session;
        const uint8_t reason = kBusy;
        reply(kRefuse, &reason, 1);
        from_ = keep;
        session_ = keep_session;
        return false;
      }
      from_ = p.from;
      session_ = session;
      if (p.size < kHeader + 4) return false;
      size_ = get32(p.data + kHeader);
      uint32_t at = kHeader + 4;
      char offered[128];
      take_string(p, at, offered, sizeof(offered));
      take_string(p, at, sender_name_, sizeof(sender_name_));
      char path[160];
      uint8_t reason = kBadName;
      if (safe_book_name(offered, name_, sizeof(name_))) {
        std::snprintf(path, sizeof(path), "books/%s", name_);
        writer_ = storage_->create(path);
        reason = kNoSpace;
      }
      if (writer_ == nullptr) {
        card_full_ = reason == kNoSpace;
        reply(kRefuse, &reason, 1);
        return card_full_;
      }
      state_ = State::Receiving;
      got_ = 0;
      next_ = 0;
      crc_ = 0;
      quiet_ = 0;
      shown_percent_ = 0;
      card_full_ = false;
      reply(kAccept, nullptr, 0);
      return true;
    }
    case kData: {
      if (state_ != State::Receiving || session != session_ || !(p.from == from_) || p.size < kHeader + 4) {
        return false;
      }
      quiet_ = 0;
      const uint32_t seq = get32(p.data + kHeader);
      const uint8_t* bytes = p.data + kHeader + 4;
      const uint32_t len = p.size - kHeader - 4;
      bool changed = false;
      if (seq == next_) {
        if (got_ + len > size_ || !writer_->write(bytes, len)) {
          const uint8_t result = got_ + len > size_ ? kCorrupt : kCardFull;
          card_full_ = result == kCardFull;
          abort();
          reply(kResult, &result, 1);
          return true;
        }
        crc_ = crc32_update(crc_, bytes, len);
        got_ += len;
        ++next_;
        const int shown = percent() / 5;
        changed = shown != shown_percent_;
        shown_percent_ = shown;
      }
      uint8_t next[4];
      put32(next, next_);
      reply(kAck, next, 4);  // also for repeats, whose answer was lost
      return changed;
    }
    case kDone: {
      if (session == done_session_ && p.from == from_ && state_ == State::Listening) {
        reply(kResult, &done_result_, 1);  // the first answer was lost
        return false;
      }
      if (state_ != State::Receiving || session != session_ || !(p.from == from_) || p.size < kHeader + 4) {
        return false;
      }
      const bool whole = got_ == size_ && get32(p.data + kHeader) == crc_;
      const bool kept = storage_->finish(writer_, whole);
      writer_ = nullptr;
      done_result_ = !whole ? kCorrupt : kept ? kSaved : kCardFull;
      done_session_ = session_;
      if (done_result_ == kSaved) {
        ++received_;
        std::snprintf(last_, sizeof(last_), "%s", name_);
      }
      card_full_ = done_result_ == kCardFull;
      state_ = State::Listening;
      reply(kResult, &done_result_, 1);
      return true;
    }
    default:
      return false;
  }
}

bool PeerReceiver::tick() {
  if (state_ != State::Receiving || ++quiet_ <= kReceiverPatience) return false;
  abort();  // the sender went away
  return true;
}

int PeerReceiver::percent() const {
  if (size_ == 0) return 100;
  return static_cast<int>(uint64_t{got_} * 100 / size_);
}

}  // namespace vellum

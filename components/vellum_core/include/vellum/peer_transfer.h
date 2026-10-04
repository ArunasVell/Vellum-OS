// Sending a book from one reader to another nearby, over Peers.
//
// The receiver listens while its "Add books" screen is open. The sender
// asks who is listening, the reader picks one, and the book goes over in
// numbered pieces, each acknowledged before the next (lost packets are sent
// again after a quiet tick). A CRC-32 at the end confirms the copy before
// the receiver keeps it. Both sides are driven by packets and by tick(),
// which the app calls when nothing has arrived for kTickMs.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

uint32_t crc32_update(uint32_t crc, const void* data, size_t len);  // start with 0

namespace peer {

constexpr uint32_t kTickMs = 300;
constexpr int kMaxReaders = 6;  // readers listed while looking
constexpr int kNameSize = 33;

}  // namespace peer

class PeerSender {
 public:
  enum class State : uint8_t {
    Idle,
    Looking,    // asking who is listening, every tick
    Offering,   // waiting for the chosen reader to accept
    Sending,    // pieces going over
    Finishing,  // waiting for the receiver to confirm the copy
    Sent,
    Failed,
  };
  enum class Error : uint8_t { None, NoAnswer, Refused, CardFull, Corrupt, CannotRead };

  struct Reader {
    PeerAddress address;
    char name[peer::kNameSize] = {};
  };

  // Starts looking for readers to send `path` (a book on `storage`) to.
  // Returns false when the book cannot be opened.
  bool begin(Peers& peers, Storage& storage, const char* path);
  // Sends the book to reader i of those found.
  void send_to(int i);
  // Stops and closes the book; nothing more is sent.
  void cancel();

  // Returns true when what the screen shows changed.
  bool on_packet(const PeerPacket& p);
  bool tick();

  State state() const { return state_; }
  Error error() const { return error_; }
  bool active() const { return state_ != State::Idle && state_ != State::Sent && state_ != State::Failed; }
  // Whether tick() still has work: false once looking has run its course.
  bool needs_ticks() const;
  int reader_count() const { return reader_count_; }
  const Reader& reader(int i) const { return readers_[i]; }
  const char* target_name() const { return readers_[target_].name; }
  int percent() const;

 private:
  void send_piece();
  void fail(Error e);

  Peers* peers_ = nullptr;
  Storage* storage_ = nullptr;
  File* file_ = nullptr;
  State state_ = State::Idle;
  Error error_ = Error::None;
  Reader readers_[peer::kMaxReaders];
  int reader_count_ = 0;
  int target_ = 0;
  uint32_t session_ = 0;
  uint32_t size_ = 0;
  uint32_t seq_ = 0;  // piece being sent
  uint32_t crc_ = 0;  // of the pieces before seq_
  int quiet_ = 0;     // ticks without an answer (while looking: ticks so far)
  int shown_percent_ = -1;
  char name_[128] = {};  // the book's file name
};

class PeerReceiver {
 public:
  enum class State : uint8_t { Off, Listening, Receiving };

  // Starts answering senders; books are saved in "books/".
  void begin(Peers& peers, Storage& storage);
  // Stops, throwing away a book that has not finished.
  void end();

  // Returns true when what the screen shows changed.
  bool on_packet(const PeerPacket& p);
  bool tick();

  State state() const { return state_; }
  bool busy() const { return state_ == State::Receiving; }
  int received() const { return received_; }
  const char* last() const { return last_; }  // file name of the newest book saved
  const char* receiving() const { return state_ == State::Receiving ? name_ : ""; }
  const char* sender() const { return sender_name_; }
  bool card_full() const { return card_full_; }
  int percent() const;

 private:
  void abort();
  void reply(uint8_t type, const void* body, uint32_t len);

  Peers* peers_ = nullptr;
  Storage* storage_ = nullptr;
  Writer* writer_ = nullptr;
  State state_ = State::Off;
  PeerAddress from_;
  uint32_t session_ = 0;
  uint32_t done_session_ = 0;  // last session finished, to answer a repeated end
  uint8_t done_result_ = 0;
  uint32_t size_ = 0;
  uint32_t got_ = 0;   // bytes written
  uint32_t next_ = 0;  // piece expected next
  uint32_t crc_ = 0;
  int quiet_ = 0;
  int shown_percent_ = -1;
  int received_ = 0;
  bool card_full_ = false;
  char name_[128] = {};
  char last_[128] = {};
  char sender_name_[peer::kNameSize] = {};
};

}  // namespace vellum

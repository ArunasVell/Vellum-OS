// Hardware API: the only interface between portable code and a device.
//
// Everything above this header (UI, reader, services) is device-agnostic.
// A board (real hardware or the desktop simulator) implements these
// interfaces and hands them to vellum::run() as a Board.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/framebuffer.h"

namespace vellum {

enum class RefreshMode : uint8_t {
  Fast,  // differential waveform, no flash; may leave ghosting
  Full,  // full waveform with flash; clears ghosting
};

class DisplayPanel {
 public:
  virtual ~DisplayPanel() = default;
  virtual int width() const = 0;   // physical pixels
  virtual int height() const = 0;  // physical pixels
  // Sends the framebuffer to the panel and refreshes. `dirty` is the changed
  // area in physical coordinates; drivers may refresh more than that.
  // Drivers power the controller down when the refresh completes.
  virtual void present(const Framebuffer& fb, RefreshMode mode, Rect dirty) = 0;
};

enum class Key : uint8_t { Prev, Next, Home, Power };

// A key press or release, or one sample of a finger on the touch screen.
// Touch coordinates are physical panel pixels, like the framebuffer's
// storage; the app maps them to logical coordinates.
struct InputEvent {
  enum class Type : uint8_t {
    None,
    KeyDown,
    KeyUp,
    TouchDown,
    TouchMove,
    TouchUp,
    Transfer,  // something changed in the book transfer; see Transfer::status()
    Peer,      // a packet from another reader is waiting; see Peers::receive()
    Quit,
  };
  Type type = Type::None;
  Key key = Key::Next;
  int16_t x = 0;
  int16_t y = 0;
  // When it happened, in milliseconds from any fixed start; 0 when the board
  // does not say. Touch events carry it so a held finger can be told apart
  // from a tap.
  uint32_t ms = 0;
};

class InputSource {
 public:
  virtual ~InputSource() = default;
  // Blocks (sleeping where possible) until an event arrives or the timeout
  // passes. Returns false on timeout.
  virtual bool wait(InputEvent& out, uint32_t timeout_ms) = 0;
  // Milliseconds counted steadily since some fixed moment (usually start),
  // unaffected by setting the clock; it may wrap. 0 when the board keeps no
  // such count: the app then times reading with the Clock.
  virtual uint32_t now_ms() { return 0; }
};

class Power {
 public:
  virtual ~Power() = default;
  virtual int battery_percent() = 0;  // 0..100, or -1 when unknown
  virtual bool charging() = 0;
  // Sleeps until the Power button is pressed, with the screen showing what
  // was last drawn. A board that restarts on waking (deep sleep) never
  // returns; one that does not (the simulators) returns at once, and the
  // app then waits for the next button press.
  virtual void sleep() {}
  // Starts the device again, after a software update. Boards that cannot
  // (the simulators) return and the app carries on.
  virtual void restart() {}
  // Asks the next sleep() to also wake by itself after `seconds` (0: only
  // the Power button wakes it), for the nightly update check.
  virtual void set_wake_timer(uint32_t seconds) { (void)seconds; }
  // True when this start is that timer waking the device, not a person.
  virtual bool woke_by_timer() { return false; }
  // True when the last run ended in a crash (a panic or a watchdog reset),
  // not in sleep or restart(): the app then starts on the home screen with
  // the built-in typeface, so a broken book or font cannot crash it again
  // and again.
  virtual bool after_crash() { return false; }
};

class Frontlight {
 public:
  virtual ~Frontlight() = default;
  // level 0 = off, 255 = brightest; warmth 0 = coolest, 255 = warmest.
  virtual void set(uint8_t level, uint8_t warmth) = 0;
};

struct DateTime {
  uint16_t year;   // e.g. 2026
  uint8_t month;   // 1..12
  uint8_t day;     // 1..31
  uint8_t hour;    // 0..23
  uint8_t minute;  // 0..59
  uint8_t second;  // 0..59
};

class Clock {
 public:
  virtual ~Clock() = default;
  // Local wall-clock time. Returns false when the time is not known yet
  // (the clock lost power and has not been set since).
  virtual bool now(DateTime& out) = 0;
  // Sets the time. Returns false when the clock cannot be set.
  virtual bool set(const DateTime&) { return false; }
};

// An open file on storage. Reads are random access; files opened for
// reading cannot be written.
class File {
 public:
  virtual ~File() = default;
  virtual uint32_t size() const = 0;
  // Reads up to len bytes at offset. Returns the bytes read, or -1 on error.
  virtual int32_t read_at(uint32_t offset, void* buf, uint32_t len) = 0;
};

// A file being written from start to end (see Storage::create).
class Writer {
 public:
  virtual ~Writer() = default;
  virtual bool write(const void* data, uint32_t len) = 0;
};

// The memory card. Paths are relative to its root and use '/', e.g.
// "books/Alice.epub".
class Storage {
 public:
  virtual ~Storage() = default;
  // Opens a file for reading; nullptr when missing. At most kMaxOpen files
  // are open at once.
  static constexpr int kMaxOpen = 4;
  virtual File* open(const char* path) = 0;
  virtual void close(File* f) = 0;
  // Replaces a whole file, creating parent directories as needed.
  virtual bool write(const char* path, const void* data, uint32_t len) = 0;
  // Calls fn for each entry in dir. Returns false when dir cannot be read.
  using ListFn = void (*)(void* ctx, const char* name, bool is_dir, uint32_t size);
  virtual bool list(const char* dir, ListFn fn, void* ctx) = 0;
  // Starts writing `path` piece by piece, for files too large to hold in
  // memory. The file appears only when finish() keeps it, so a broken
  // transfer leaves nothing behind. One writer at a time; nullptr on error.
  virtual Writer* create(const char*) { return nullptr; }
  // Closes the writer; `keep` replaces `path` with what was written, else it
  // is thrown away. Returns false when keeping failed.
  virtual bool finish(Writer*, bool) { return false; }
};

// Receiving books from a phone or computer over Wi-Fi. The device opens its
// own Wi-Fi network (or joins the home Wi-Fi, when asked) and serves an
// upload page; books land in "books/" on the storage. While it runs, the
// board posts InputEvent::Type::Transfer whenever the status changes.
struct TransferInfo {
  char network[33] = {};   // Wi-Fi network name
  char password[65] = {};  // its password ("" on the home Wi-Fi: the phone knows it)
  char address[64] = {};   // upload page, e.g. "http://192.168.4.1"
  // Set by the app: serve the page on the home Wi-Fi (the network the
  // Updater uses) instead of the reader's own. start() returns false when
  // it cannot be joined; a board that cannot do it at all clears `home`
  // and opens its own network.
  bool home = false;
  // Set by the app, for the page's Settings: the typefaces it can pick, one
  // name per line. Stays valid until stop().
  const char* fonts = nullptr;
};

struct TransferStatus {
  // A software update sent from the upload page.
  enum class Update : uint8_t {
    None,
    Receiving,  // update_percent tells how far
    Installed,  // the next start runs it
    NotVellum,  // the file was not a Vellum update for this device; nothing changed
    Broken,     // it did not arrive whole; nothing changed
  };

  int devices = 0;           // phones or computers joined to the network
  int received = 0;          // books saved since start()
  char last[128] = {};       // file name of the newest book saved
  char receiving[128] = {};  // file name being received now, or ""
  bool card_full = false;    // the last book did not fit
  Update update = Update::None;
  int update_percent = 0;
  char update_version[32] = {};  // version of the update being installed
  int home_wifi_saved = 0;       // counts home Wi-Fi settings saved from the page
  // Counts changes the page saved on the card for the app to take up: its
  // settings (vellum/settings.txt) and the sync account (vellum/kosync.txt).
  int settings_saved = 0;
};

class Transfer {
 public:
  virtual ~Transfer() = default;
  // Starts the network and the upload page. Returns false if Wi-Fi cannot start.
  virtual bool start(TransferInfo& info) = 0;
  // Stops both and turns the radio off.
  virtual void stop() = 0;
  virtual void status(TransferStatus& out) = 0;
};

// New versions of Vellum from the internet: the device joins the home
// Wi-Fi network set from the phone page, asks the release server for the
// newest version and installs it into the spare slot. Calls block for a
// few seconds (a whole download for install()) and leave the radio off.
enum class UpdateResult : uint8_t {
  UpToDate,
  Available,  // a newer version is out; check() fills in its number
  Installed,  // it runs after restart()
  NoNetwork,  // no home Wi-Fi is set
  NoWifi,     // the home Wi-Fi could not be joined
  NoServer,   // the release server could not be reached or read
  Failed,     // the download broke off or was not a Vellum update; nothing changed
};

class Updater {
 public:
  virtual ~Updater() = default;
  // The home Wi-Fi network's name, or "" when none is set.
  virtual void home_network(char* out, size_t size) = 0;
  // Asks for the newest release. Available when it is newer than `current`.
  virtual UpdateResult check(const char* current, char* latest, size_t size) = 0;
  // Downloads and installs the newest release; progress(ctx, percent) is
  // called now and then.
  using ProgressFn = void (*)(void* ctx, int percent);
  virtual UpdateResult install(ProgressFn progress, void* ctx) = 0;
};

// Files from the internet over the home Wi-Fi (the same network the Updater
// uses), for the free book store. The first get() joins the network; the
// radio stays on until disconnect(), so browsing a catalogue does not join
// again for every page. Calls block until the whole answer has arrived.
enum class NetResult : uint8_t {
  Ok,
  NoNetwork,  // no home Wi-Fi is set
  NoWifi,     // the home Wi-Fi could not be joined
  NoServer,   // the server could not be reached, or did not answer 200
  BrokeOff,   // the answer started but did not arrive whole
  Stopped,    // the body callback asked to stop
};

// A request with a method, headers or a body: for services that answer more
// than files (KOReader sync) or ask for a password (catalogues).
struct HttpRequest {
  const char* method = "GET";  // "GET", "POST" or "PUT"
  const char* url = nullptr;
  const char* headers = nullptr;  // extra header lines, each "Name: value\r\n"; nullptr for none
  const char* body = nullptr;     // sent as it is; nullptr for none
  uint32_t body_len = 0;
  int status = 0;  // set by Network::request(): the answer's status, 0 when none came
};

class Network {
 public:
  virtual ~Network() = default;
  // HTTPS (or HTTP) GET of `url`, following redirects. The body arrives in
  // pieces: body(ctx, data, len, total) where total is the whole length
  // when the server says (else 0). Returning false stops the transfer.
  using BodyFn = bool (*)(void* ctx, const uint8_t* data, uint32_t len, uint32_t total);
  virtual NetResult get(const char* url, BodyFn body, void* ctx) = 0;
  // Sends `req` (a GET follows redirects) and hands the answer's body to
  // body() like get(), whatever its status, which lands in req.status. Ok
  // when an answer arrived whole, a 401 or a 404 too. NoServer from boards
  // that cannot.
  virtual NetResult request(HttpRequest& req, BodyFn body, void* ctx) {
    (void)req;
    (void)body;
    (void)ctx;
    return NetResult::NoServer;
  }
  // Leaves the network and turns the radio off.
  virtual void disconnect() = 0;
  // True for the simulators' canned catalogue, which the store marks as such.
  virtual bool demo() const { return false; }
};

// Short messages straight to other readers nearby, with no Wi-Fi network in
// between (ESP-NOW on the X4 Pro). Used to send books reader to reader.
// While started, the board posts InputEvent::Type::Peer when packets arrive.
struct PeerAddress {
  uint8_t bytes[6] = {};
  bool operator==(const PeerAddress&) const = default;
};

struct PeerPacket {
  static constexpr int kMaxSize = 1400;
  PeerAddress from;
  uint16_t size = 0;
  uint8_t data[kMaxSize];
};

class Peers {
 public:
  virtual ~Peers() = default;
  // Turns the radio on and starts listening. Returns false when it cannot.
  virtual bool start() = 0;
  // Stops listening and turns the radio off (unless Transfer still uses it).
  virtual void stop() = 0;
  // This reader's name as others see it, e.g. "Vellum-3F2A".
  virtual const char* name() = 0;
  // Sends up to PeerPacket::kMaxSize bytes; `to` = nullptr reaches everyone
  // in range. Delivery is not guaranteed.
  virtual bool send(const PeerAddress* to, const void* data, uint32_t len) = 0;
  // Takes the oldest packet received; false when there is none.
  virtual bool receive(PeerPacket& out) = 0;
};

// Free memory, for the device check.
struct MemoryInfo {
  uint32_t internal_free = 0;  // bytes of the chip's own RAM
  uint32_t external_free = 0;  // bytes of external RAM (PSRAM), 0 when none
};

struct Board {
  const char* name;
  DisplayPanel& display;
  InputSource& input;
  Power& power;
  Frontlight* frontlight;        // nullptr when the device has none
  Clock* clock;                  // nullptr when the device has none
  Storage* storage;              // nullptr when there is no card
  Rotation rotation;             // logical orientation for reading
  uint8_t* framebuffer;          // Framebuffer::bytes_for(display size) bytes
  Transfer* transfer = nullptr;  // nullptr when the device has no Wi-Fi
  Peers* peers = nullptr;        // nullptr when it cannot reach other readers
  const char* version = "dev";   // the software version running
  Updater* updater = nullptr;    // nullptr when the device cannot update itself
  Network* network = nullptr;    // nullptr when it cannot reach the internet
  // Fills in the free memory now; nullptr when the board does not tell.
  void (*memory)(MemoryInfo& out) = nullptr;
};

}  // namespace vellum

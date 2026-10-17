// Browser simulator glue: exposes the core app to JavaScript.
//
// The page owns the event loop. It calls vellum_start() once, vellum_key()
// per button press (or vellum_key_event() as a button goes down and up, so
// it can be held), and after each call reads the latest frame when
// vellum_frame_count() has changed. vellum_last_full() tells it to play the
// e-ink flash for a full refresh.
#include <emscripten/emscripten.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include "../demo_network.h"
#include "../demo_updater.h"
#include "vellum/app.h"
#include "vellum/highlights.h"
#include "vellum/stdio_storage.h"

namespace {

constexpr int kPanelW = 800;
constexpr int kPanelH = 480;

class WebDisplay : public vellum::DisplayPanel {
 public:
  int width() const override { return kPanelW; }
  int height() const override { return kPanelH; }

  void present(const vellum::Framebuffer& fb, vellum::RefreshMode mode, vellum::Rect) override {
    w_ = fb.width();
    h_ = fb.height();
    rgba_.resize(static_cast<size_t>(w_) * h_ * 4);
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        uint8_t* p = &rgba_[(static_cast<size_t>(y) * w_ + x) * 4];
        const bool white = fb.pixel(x, y) == vellum::Color::White;
        // E-ink tones: warm paper and soft ink rather than pure white/black.
        p[0] = white ? 0xEC : 0x24;
        p[1] = white ? 0xEA : 0x24;
        p[2] = white ? 0xE4 : 0x27;
        p[3] = 0xFF;
      }
    }
    last_full_ = mode == vellum::RefreshMode::Full;
    rotation_ = fb.rotation();
    ++frames_;
  }

  std::vector<uint8_t> rgba_;
  int w_ = 0;
  int h_ = 0;
  int frames_ = 0;
  bool last_full_ = false;
  vellum::Rotation rotation_ = vellum::Rotation::R90;  // touches are points on the last frame
};

class NoInput : public vellum::InputSource {
 public:
  bool wait(vellum::InputEvent&, uint32_t) override { return false; }
  uint32_t now_ms() override { return static_cast<uint32_t>(emscripten_get_now()); }
};

class WebPower : public vellum::Power {
 public:
  int battery_percent() override { return percent; }
  bool charging() override { return false; }
  int percent = 82;
};

// The page tints the screen to show the front light.
class WebFrontlight : public vellum::Frontlight {
 public:
  void set(uint8_t level, uint8_t warmth) override {
    EM_ASM(
        {
          if (window.vellumLight) window.vellumLight($0, $1);
        },
        level, warmth);
  }
};

// The browser's local time.
class WebClock : public vellum::Clock {
 public:
  bool now(vellum::DateTime& out) override {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) return false;
    out = {static_cast<uint16_t>(local.tm_year + 1900), static_cast<uint8_t>(local.tm_mon + 1),
           static_cast<uint8_t>(local.tm_mday),         static_cast<uint8_t>(local.tm_hour),
           static_cast<uint8_t>(local.tm_min),          static_cast<uint8_t>(local.tm_sec)};
    return true;
  }
};

// The "Add books" screen without a network: books added on the page count
// as received while the screen is open.
class WebTransfer : public vellum::Transfer {
 public:
  bool start(vellum::TransferInfo& info) override {
    if (info.home) {
      std::snprintf(info.network, sizeof(info.network), "Home (simulated)");
      std::snprintf(info.address, sizeof(info.address), "http://192.168.1.42");
    } else {
      std::snprintf(info.network, sizeof(info.network), "Vellum-SIM");
      std::snprintf(info.password, sizeof(info.password), "48151623");
      std::snprintf(info.address, sizeof(info.address), "http://192.168.4.1");
    }
    st = {};
    running = true;
    return true;
  }
  void stop() override { running = false; }
  void status(vellum::TransferStatus& out) override { out = st; }

  vellum::TransferStatus st;
  bool running = false;
};

// Other readers are other tabs with this page open: the page carries packets
// between them over a BroadcastChannel.
class WebPeers : public vellum::Peers {
 public:
  bool start() override {
    started = true;
    inbox.clear();
    return true;
  }
  void stop() override {
    started = false;
    inbox.clear();
  }
  const char* name() override { return name_; }
  bool send(const vellum::PeerAddress* to, const void* data, uint32_t len) override {
    if (!started) return false;
    EM_ASM(
        {
          if (window.vellumPeerSend) window.vellumPeerSend($0, $1, $2, $3);
        },
        to != nullptr ? to->bytes : nullptr, address.bytes, data, len);
    return true;
  }
  bool receive(vellum::PeerPacket& out) override {
    if (inbox.empty()) return false;
    out = inbox.front();
    inbox.pop_front();
    return true;
  }
  void pick_name() {
    for (uint8_t& b : address.bytes) b = static_cast<uint8_t>(EM_ASM_INT({ return Math.floor(Math.random() * 256); }));
    std::snprintf(name_, sizeof(name_), "Vellum-%02X%02X", address.bytes[4], address.bytes[5]);
  }

  bool started = false;
  vellum::PeerAddress address;
  std::deque<vellum::PeerPacket> inbox;
  uint8_t incoming[6 + vellum::PeerPacket::kMaxSize];  // the page writes the sender, then the bytes
  char name_[16] = "Vellum";
};

WebDisplay display;
WebTransfer transfer;
DemoUpdater updater;
// The store's canned catalogue: build.sh embeds the test feeds at /demo.
// (Fetching the real catalogues from a web page would need the sites to
// allow cross-origin requests, and the app's blocking calls to wait on
// asynchronous fetches.)
DemoNetwork network("/demo");
NoInput input;
WebPower power;
WebPeers peers;
WebClock clock;
WebFrontlight light;
// The page writes dropped books into /card/books in Emscripten's in-memory file system.
vellum::StdioStorage card("/card");
std::vector<uint8_t> framebuffer(vellum::Framebuffer::bytes_for(kPanelW, kPanelH));
vellum::Board board{"Browser simulator", display,   input, power, &light, &clock, &card, vellum::Rotation::R90,
                    framebuffer.data(),  &transfer, &peers};
vellum::App* app = nullptr;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void vellum_start(int battery) {
  power.percent = battery;
  board.version = "sim";
  board.builtin_pictures = true;
  board.updater = &updater;
  board.network = &network;
  peers.pick_name();
  static vellum::App instance(board);
  app = &instance;
  app->start();
}

// A short press: 0 = previous, 1 = next, 2 = home, 3 = power
EMSCRIPTEN_KEEPALIVE void vellum_key(int key) {
  if (!app || key < 0 || key > 3) return;
  vellum::InputEvent ev;
  ev.type = vellum::InputEvent::Type::KeyDown;
  ev.key = static_cast<vellum::Key>(key);
  app->handle(ev);
  ev.type = vellum::InputEvent::Type::KeyUp;
  app->handle(ev);
}

// A button going down (down = 1) or up (0), for presses that can be held;
// vellum_poll() tells the app when one has been held long enough.
EMSCRIPTEN_KEEPALIVE void vellum_key_event(int key, int down) {
  if (!app || key < 0 || key > 3) return;
  vellum::InputEvent ev;
  ev.type = down != 0 ? vellum::InputEvent::Type::KeyDown : vellum::InputEvent::Type::KeyUp;
  ev.key = static_cast<vellum::Key>(key);
  app->handle(ev);
}

// Power held down long enough to sleep.
EMSCRIPTEN_KEEPALIVE void vellum_hold_power() {
  if (!app) return;
  vellum::InputEvent ev;
  ev.type = vellum::InputEvent::Type::KeyDown;
  ev.key = vellum::Key::Power;
  app->handle(ev);
  app->timeout();
  ev.type = vellum::InputEvent::Type::KeyUp;
  app->handle(ev);
}

// 0 = finger down, 1 = move, 2 = up; (x, y) in logical pixels of the last frame;
// ms is when, in milliseconds (a finger held still is a long press).
EMSCRIPTEN_KEEPALIVE void vellum_touch(int kind, int x, int y, int ms) {
  if (!app || kind < 0 || kind > 2) return;
  constexpr vellum::InputEvent::Type kTypes[] = {
      vellum::InputEvent::Type::TouchDown, vellum::InputEvent::Type::TouchMove, vellum::InputEvent::Type::TouchUp};
  vellum::InputEvent ev;
  ev.type = kTypes[kind];
  int px, py;
  vellum::rotate_to_physical(display.rotation_, kPanelW, kPanelH, x, y, px, py);
  ev.x = static_cast<int16_t>(px < 0 ? 0 : px >= kPanelW ? kPanelW - 1 : px);
  ev.y = static_cast<int16_t>(py < 0 ? 0 : py >= kPanelH ? kPanelH - 1 : py);
  ev.ms = static_cast<uint32_t>(ms);
  app->handle(ev);
}

// The phone page's highlights, while "Add books" is open: the page lists
// them and sets notes, as a phone would through the reader's server.
EMSCRIPTEN_KEEPALIVE int vellum_transfer_open() { return transfer.running ? 1 : 0; }

vellum::Highlights* phone_highlights() {
  static vellum::Highlights* h = new vellum::Highlights;
  h->load(card);
  return h;
}

EMSCRIPTEN_KEEPALIVE const char* vellum_highlights_json() {
  static std::string json;
  json.clear();
  phone_highlights()->write_json(
      [](void* ctx, const char* text, size_t len) { static_cast<std::string*>(ctx)->append(text, len); }, &json);
  return json.c_str();
}

EMSCRIPTEN_KEEPALIVE int vellum_highlight_note(int id, const char* note) {
  vellum::Highlights* h = phone_highlights();
  return h->set_note(static_cast<uint32_t>(id), note) && h->save(card) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int vellum_highlight_remove(int id) {
  vellum::Highlights* h = phone_highlights();
  const int k = h->find(static_cast<uint32_t>(id));
  if (k < 0) return 0;
  h->remove(k);
  return h->save(card) ? 1 : 0;
}

// Call after writing a book into /card/books. Returns 1 when the "Add books"
// screen is open and took it (the library reloads when that screen closes),
// 0 when the caller should call vellum_reload_library() itself.
EMSCRIPTEN_KEEPALIVE int vellum_book_received(const char* name) {
  if (!app || !transfer.running) return 0;
  ++transfer.st.received;
  std::snprintf(transfer.st.last, sizeof(transfer.st.last), "%s", name);
  vellum::InputEvent ev;
  ev.type = vellum::InputEvent::Type::Transfer;
  app->handle(ev);
  return 1;
}

// Call after writing books into /card/books.
EMSCRIPTEN_KEEPALIVE void vellum_reload_library() {
  if (app) app->reload_library();
}

// Another tab's packet, written into vellum_peer_buffer(): 6 address bytes,
// then `len` bytes of the packet.
EMSCRIPTEN_KEEPALIVE uint8_t* vellum_peer_buffer() { return peers.incoming; }
EMSCRIPTEN_KEEPALIVE const uint8_t* vellum_peer_address() { return peers.address.bytes; }
EMSCRIPTEN_KEEPALIVE void vellum_peer_packet(int len) {
  if (!app || !peers.started || len < 0 || len > vellum::PeerPacket::kMaxSize || peers.inbox.size() > 64) return;
  vellum::PeerPacket& p = peers.inbox.emplace_back();
  std::memcpy(p.from.bytes, peers.incoming, 6);
  std::memcpy(p.data, peers.incoming + 6, static_cast<size_t>(len));
  p.size = static_cast<uint16_t>(len);
  vellum::InputEvent ev;
  ev.type = vellum::InputEvent::Type::Peer;
  app->handle(ev);
}

// Called every few hundred milliseconds: stands in for the device's input
// wait timing out, which keeps a transfer going when packets are lost, and
// tells held buttons, page turns and notices that their time has come.
EMSCRIPTEN_KEEPALIVE void vellum_poll() {
  if (app && app->wait_ms() <= vellum::peer::kTickMs) app->timeout();
}

EMSCRIPTEN_KEEPALIVE int vellum_frame_count() { return display.frames_; }
EMSCRIPTEN_KEEPALIVE int vellum_last_full() { return display.last_full_ ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int vellum_width() { return display.w_; }
EMSCRIPTEN_KEEPALIVE int vellum_height() { return display.h_; }
EMSCRIPTEN_KEEPALIVE const uint8_t* vellum_pixels() { return display.rgba_.data(); }
}

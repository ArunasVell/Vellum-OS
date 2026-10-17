// Solomon SSD1677, ported from FreeInk's Ssd1677Driver (MIT) with the stock
// X4 / X4 Pro sequences:
//   init: 0C = AE C7 C3 C0 80, 3C = 80
//   full: 3C = C0, 22 = F7, 20
//   half: 3C = C0, 1A = 5A, 22 = D7, 20
//   fast: 3C = C0, 22 = FC, 20
// The controller keeps two RAM planes: BW holds the new frame and RED the one
// on screen, which the fast waveform diffs against.
#include "epd_controller.h"

namespace epd {
namespace {

constexpr uint8_t kSoftReset = 0x12;
constexpr uint8_t kTempSensor = 0x18;
constexpr uint8_t kBoosterSoftStart = 0x0C;
constexpr uint8_t kDriverOutput = 0x01;
constexpr uint8_t kBorderWaveform = 0x3C;
constexpr uint8_t kDataEntryMode = 0x11;
constexpr uint8_t kRamXRange = 0x44;
constexpr uint8_t kRamYRange = 0x45;
constexpr uint8_t kRamXCounter = 0x4E;
constexpr uint8_t kRamYCounter = 0x4F;
constexpr uint8_t kWriteRamBw = 0x24;
constexpr uint8_t kWriteRamRed = 0x26;
constexpr uint8_t kAutoWriteBw = 0x46;
constexpr uint8_t kAutoWriteRed = 0x47;
constexpr uint8_t kUpdateCtrl1 = 0x21;
constexpr uint8_t kUpdateCtrl2 = 0x22;
constexpr uint8_t kMasterActivation = 0x20;
constexpr uint8_t kWriteTemp = 0x1A;
constexpr uint8_t kDeepSleep = 0x10;

constexpr uint8_t kCtrl1Normal = 0x00;     // diff BW against RED
constexpr uint8_t kCtrl1BypassRed = 0x40;  // treat RED as blank

constexpr uint8_t kSeqFull = 0xF7;
constexpr uint8_t kSeqHalf = 0xD7;
constexpr uint8_t kSeqFast = 0xFC;
constexpr uint8_t kBorderInit = 0x80;
constexpr uint8_t kBorderRefresh = 0xC0;
constexpr uint8_t kHalfTemp = 0x5A;

constexpr uint32_t kRefreshTimeoutMs = 10000;

class Ssd1677 : public Controller {
 public:
  Ssd1677(uint16_t w, uint16_t h) : w_(w), h_(h) {}

  const char* name() const override { return "SSD1677"; }
  uint32_t spi_hz() const override { return 10000000; }

  void begin(Bus& bus) override {
    bus.reset();
    bus.cmd(kSoftReset);
    Bus::delay_ms(10);
    wait_idle(bus);
    bus.cmd_data(kTempSensor, {0x80});  // internal sensor
    bus.cmd_data(kBoosterSoftStart, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});
    bus.cmd_data(kDriverOutput, {lo(h_ - 1), hi(h_ - 1), 0x02});
    bus.cmd_data(kBorderWaveform, {kBorderInit});
    set_ram_area(bus);
    bus.cmd_data(kAutoWriteBw, {0xF7});
    wait_idle(bus);
    bus.cmd_data(kAutoWriteRed, {0xF7});
    wait_idle(bus);
    powered_ = false;
    first_ = true;
  }

  void display(Bus& bus, const uint8_t* frame, Mode mode, bool power_off) override {
    // RED is blank after init, so the first paint must not be differential.
    if (first_ && mode == Mode::Fast) mode = Mode::Half;
    first_ = false;

    set_ram_area(bus);
    write_ram(bus, kWriteRamBw, frame);
    if (mode != Mode::Fast) write_ram(bus, kWriteRamRed, frame);

    bus.cmd_data(kUpdateCtrl1, {mode == Mode::Fast ? kCtrl1Normal : kCtrl1BypassRed});
    bus.cmd_data(kBorderWaveform, {kBorderRefresh});
    if (mode == Mode::Half) bus.cmd_data(kWriteTemp, {kHalfTemp});
    const uint8_t seq = mode == Mode::Fast ? kSeqFast : mode == Mode::Half ? kSeqHalf : kSeqFull;
    bus.cmd_data(kUpdateCtrl2, {seq});
    bus.cmd(kMasterActivation);
    wait_refresh(bus);
    // Sequences ending in 0x03 switch the analog supply off themselves.
    powered_ = (seq & 0x03) == 0;

    // RED must hold what is now on screen for the next fast update. (BW
    // already does; FreeInk rewrites both, one plane is enough.)
    if (mode == Mode::Fast) {
      set_ram_area(bus);
      write_ram(bus, kWriteRamRed, frame);
    }
    if (power_off) power_down(bus);
  }

  void power_off(Bus& bus) override { power_down(bus); }

  void deep_sleep(Bus& bus) override {
    power_down(bus);
    bus.cmd_data(kDeepSleep, {0x03});
  }

 private:
  static uint8_t lo(int v) { return static_cast<uint8_t>(v & 0xFF); }
  static uint8_t hi(int v) { return static_cast<uint8_t>((v >> 8) & 0xFF); }

  // BUSY is high while the controller works.
  static void wait_idle(Bus& bus) { bus.wait_for(0, kRefreshTimeoutMs); }
  static void wait_refresh(Bus& bus) {
    // Give BUSY a moment to rise so a fast poll cannot mistake "not started" for "done".
    bus.wait_for(1, 20);
    wait_idle(bus);
  }

  // Full-screen window; X increments, Y runs from the bottom up.
  void set_ram_area(Bus& bus) {
    bus.cmd_data(kDataEntryMode, {0x01});
    bus.cmd_data(kRamXRange, {0, 0, lo(w_ - 1), hi(w_ - 1)});
    bus.cmd_data(kRamYRange, {lo(h_ - 1), hi(h_ - 1), 0, 0});
    bus.cmd_data(kRamXCounter, {0, 0});
    bus.cmd_data(kRamYCounter, {lo(h_ - 1), hi(h_ - 1)});
  }

  void write_ram(Bus& bus, uint8_t ram, const uint8_t* frame) {
    bus.stream_begin(ram);
    bus.stream_bytes(frame, static_cast<size_t>(w_ / 8) * h_);
    bus.stream_end();
  }

  void power_down(Bus& bus) {
    if (!powered_) return;
    bus.cmd_data(kBorderWaveform, {kBorderInit});
    bus.cmd_data(kUpdateCtrl2, {0x03});  // analog off, clock off
    bus.cmd(kMasterActivation);
    // Like a refresh: BUSY rises while the supply winds down. (This waited a
    // fixed 200 ms before; the controller says when it is done.)
    bus.wait_for(1, 20, /*quiet=*/true);
    wait_idle(bus);
    powered_ = false;
  }

  const uint16_t w_;
  const uint16_t h_;
  bool powered_ = false;
  bool first_ = true;
};

}  // namespace

Controller& ssd1677(uint16_t width, uint16_t height) {
  static Ssd1677 c(width, height);
  return c;
}

}  // namespace epd

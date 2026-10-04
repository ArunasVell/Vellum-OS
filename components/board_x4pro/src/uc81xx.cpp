// UltraChip UC8179 and UC8279 (X4 Pro 800x480 variant), black and white paths,
// ported from FreeInk's Uc8179Driver and Uc8279X4Driver (MIT).
//
// Both address the glass as 800x600 with 480 visible rows and use the KW
// differential scheme: DTM2 (0x13) takes the new frame, DTM1 (0x10) the frame
// on screen. Waveforms come from the panel's OTP. BUSY_N is low while busy.
//
// Differences between the two parts, all from the hardware-validated FreeInk code:
//   UC8179: rows are sent bottom-up; the 2-byte CDI changes between refresh and
//           idle; booster and power-save are programmed at init.
//   UC8279: rows are sent top-down after a 120-row gate offset; 1-byte CDI;
//           PLL at init; fast refresh needs a partial window (PTL); PSR only
//           latches when written after power-on.
#include "epd_controller.h"

namespace epd {
namespace {

constexpr uint8_t kPanelSetting = 0x00;  // PSR
constexpr uint8_t kPowerOff = 0x02;      // POF
constexpr uint8_t kPowerOffSeq = 0x03;   // PFS
constexpr uint8_t kPowerOn = 0x04;       // PON
constexpr uint8_t kBooster = 0x06;       // BTST
constexpr uint8_t kDeepSleep = 0x07;     // DSLP, check code 0xA5
constexpr uint8_t kDtm1 = 0x10;          // old plane
constexpr uint8_t kRefresh = 0x12;       // DRF
constexpr uint8_t kDtm2 = 0x13;          // new plane
constexpr uint8_t kPll = 0x30;
constexpr uint8_t kVcomInterval = 0x50;   // CDI
constexpr uint8_t kResolution = 0x61;     // TRES
constexpr uint8_t kGateStart = 0x65;      // GSST
constexpr uint8_t kPartialWindow = 0x90;  // PTL
constexpr uint8_t kPartialIn = 0x91;      // PTIN
constexpr uint8_t kPartialOut = 0x92;     // PTOUT
constexpr uint8_t kCascade = 0xE0;        // CCSET
constexpr uint8_t kGateScan = 0xE1;
constexpr uint8_t kPowerSave = 0xE3;  // PWS
constexpr uint8_t kForceTemp = 0xE5;  // TSSET

constexpr uint16_t kAddressedRows = 600;
constexpr uint32_t kRefreshTimeoutMs = 10000;

struct Variant {
  const char* name;
  bool is_8279;
  uint8_t psr0;  // REG bit (0x20) is cleared at refresh so the OTP waveform runs
  uint8_t psr1;
  uint8_t pfs;
  uint8_t gate_scan;
  uint8_t ccset;
  uint8_t temp_full;
  uint8_t temp_fast;  // also speeds up the frame rate
  uint16_t gate_offset;
};

constexpr Variant kUc8179 = {"UC8179", false, 0x3F, 0x0A, 0x20, 0x02, 0x02, 0x1E, 0x5A, 0};
constexpr Variant kUc8279 = {"UC8279", true, 0x37, 0x4D, 0x20, 0x02, 0x02, 0x1E, 0x5A, 120};

// UC8179 only.
constexpr uint8_t kBtst[4] = {0x25, 0x25, 0x3C, 0x25};
constexpr uint8_t kPws = 0x22;
constexpr uint8_t kCdiActive = 0x29;
constexpr uint8_t kCdiIdle = 0xA9;
constexpr uint8_t kCdiInterval = 0x07;
// UC8279 only.
constexpr uint8_t kPllX4Pro = 0x0E;
constexpr uint8_t kCdiFull = 0x97;
constexpr uint8_t kCdiFast = 0xD7;

class Uc81xx : public Controller {
 public:
  Uc81xx(const Variant& v, uint16_t w, uint16_t h) : v_(v), w_(w), h_(h), wb_(w / 8) {}

  const char* name() const override { return v_.name; }
  uint32_t spi_hz() const override { return 10000000; }

  void begin(Bus& bus) override {
    bus.reset(50);
    bus.cmd_data(kPanelSetting, {v_.psr0, v_.psr1});
    bus.cmd_data(kResolution, {hi(w_), lo(w_), hi(kAddressedRows), lo(kAddressedRows)});
    bus.cmd_data(kGateStart, {0, 0, 0, 0});
    bus.cmd_data(kPowerOffSeq, {v_.pfs});
    if (v_.is_8279) {
      bus.cmd_data(kPll, {kPllX4Pro});
    } else {
      bus.cmd_data(kBooster, {kBtst[0], kBtst[1], kBtst[2], kBtst[3]});
    }
    bus.cmd_data(kGateScan, {v_.gate_scan});
    if (!v_.is_8279) bus.cmd_data(kPowerSave, {kPws});
    on_ = false;
    old_valid_ = false;
  }

  void display(Bus& bus, const uint8_t* frame, Mode mode, bool power_off) override {
    const bool fast = mode == Mode::Fast && old_valid_;

    send_plane(bus, kDtm2, frame, false);
    if (!fast) {
      if (mode == Mode::Half) {
        // Scrub: old = inverse of the target, so every pixel goes through a transition.
        send_plane(bus, kDtm1, frame, true);
      } else {
        // Full: start from an all-white old plane across every addressed row.
        bus.stream_begin(kDtm1);
        bus.stream_fill(0xFF, static_cast<size_t>(wb_) * kAddressedRows);
        bus.stream_end();
      }
    }

    start_refresh(bus, fast);
    wait_idle(bus);
    if (fast) bus.cmd(kPartialOut);
    if (!v_.is_8279) bus.cmd_data(kVcomInterval, {kCdiIdle, kCdiInterval});

    // The old plane now matches the screen for the next fast update.
    send_plane(bus, kDtm1, frame, false);
    old_valid_ = true;

    if (power_off && on_) {
      bus.cmd(kPowerOff);
      wait_idle(bus);
      on_ = false;
    }
  }

  void deep_sleep(Bus& bus) override {
    if (on_) {
      bus.cmd(kPowerOff);
      wait_idle(bus);
      on_ = false;
    }
    bus.cmd_data(kDeepSleep, {0xA5});
    old_valid_ = false;
  }

 private:
  static uint8_t lo(int v) { return static_cast<uint8_t>(v & 0xFF); }
  static uint8_t hi(int v) { return static_cast<uint8_t>((v >> 8) & 0xFF); }

  // BUSY_N reads low while busy; FreeInk waits one tick before sampling.
  static void wait_idle(Bus& bus) {
    Bus::delay_ms(1);
    bus.wait_for(1, kRefreshTimeoutMs);
  }

  void send_plane(Bus& bus, uint8_t ram, const uint8_t* frame, bool invert) {
    const size_t pad_after = static_cast<size_t>(kAddressedRows - v_.gate_offset - h_) * wb_;
    bus.stream_begin(ram);
    bus.stream_fill(0xFF, static_cast<size_t>(v_.gate_offset) * wb_);
    if (v_.is_8279) {
      bus.stream_bytes(frame, static_cast<size_t>(wb_) * h_, invert);
    } else {
      for (int y = h_ - 1; y >= 0; --y) bus.stream_bytes(frame + static_cast<size_t>(y) * wb_, wb_, invert);
    }
    bus.stream_fill(0xFF, pad_after);
    bus.stream_end();
  }

  void start_refresh(Bus& bus, bool fast) {
    if (v_.is_8279) {
      bus.cmd_data(kVcomInterval, {fast ? kCdiFast : kCdiFull});
    } else {
      bus.cmd_data(kVcomInterval, {kCdiActive, kCdiInterval});
    }
    bus.cmd_data(kCascade, {v_.ccset});
    bus.cmd_data(kForceTemp, {fast ? v_.temp_fast : v_.temp_full});
    const uint8_t psr0 = static_cast<uint8_t>(v_.psr0 & 0xDF);
    if (!v_.is_8279) bus.cmd_data(kPanelSetting, {psr0, v_.psr1});
    if (fast) {
      bus.cmd_data(kPowerOffSeq, {v_.pfs});
      bus.cmd_data(kGateScan, {v_.gate_scan});
    }

    if (!on_) {
      bus.cmd(kPowerOn);
      wait_idle(bus);
      on_ = true;
    }

    if (fast) {
      bus.cmd(kPartialIn);
      if (v_.is_8279) {
        // Without a window the UC8279 runs the waveform but develops nothing.
        const int x_end = w_ - 1;
        const int y_start = v_.gate_offset;
        const int y_end = v_.gate_offset + h_ - 1;
        bus.cmd_data(kPartialWindow, {0, 0, hi(x_end), static_cast<uint8_t>(lo(x_end) | 0x07), hi(y_start), lo(y_start),
                                      hi(y_end), lo(y_end), 0x01});
      }
    }
    if (v_.is_8279) bus.cmd_data(kPanelSetting, {psr0, v_.psr1});
    bus.cmd(kRefresh);
    // Confirm the waveform started (BUSY_N dropped) before waiting for it to end.
    bus.wait_for(0, 50);
  }

  const Variant& v_;
  const uint16_t w_;
  const uint16_t h_;
  const uint16_t wb_;
  bool on_ = false;
  bool old_valid_ = false;
};

}  // namespace

Controller& uc8179(uint16_t width, uint16_t height) {
  static Uc81xx c(kUc8179, width, height);
  return c;
}

Controller& uc8279(uint16_t width, uint16_t height) {
  static Uc81xx c(kUc8279, width, height);
  return c;
}

}  // namespace epd

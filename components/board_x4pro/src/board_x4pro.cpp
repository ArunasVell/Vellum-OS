#include "board_x4pro.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "bm8563.h"
#include "cw2017.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "epd_controller.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_now_peers.h"
#include "esp_ota_ops.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gt911.h"
#include "http_network.h"
#include "ota_updater.h"
#include "sd_card.h"
#include "sdkconfig.h"
#include "vellum/stdio_storage.h"
#include "wifi_transfer.h"

namespace {

constexpr const char* kTag = "x4pro";
constexpr int kDebounceMs = 10;

// What the boot summary reports, gathered while the board starts.
struct BootInfo {
  const char* controller = "?";
  bool card = false;
};
BootInfo g_boot;

const char* reset_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "power on";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_SW:
      return "restart";
    case ESP_RST_PANIC:
      return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout (battery)";
    default:
      return "other";
  }
}

// A crash or a hang the watchdog caught ended the last run.
bool ended_in_crash(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

void memory_info(vellum::MemoryInfo& out) {
  out.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  out.external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

// One line once the first screen is up, and warnings when internal RAM or
// the app's stack run low later (checked between events).
void log_memory(bool first) {
  constexpr size_t kLowInternal = 24 * 1024;  // Wi-Fi needs more than this to start
  constexpr size_t kLowStack = 2 * 1024;
  static bool warned_heap = false;
  static size_t warned_stack = SIZE_MAX;
  const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t stack = uxTaskGetStackHighWaterMark(nullptr);  // bytes, the least ever free
  if (first) {
    ESP_LOGI(kTag, "boot: Vellum %s, panel %s, card %s, start: %s, first screen at %d ms",
             esp_app_get_description()->version, g_boot.controller, g_boot.card ? "mounted" : "none",
             reset_name(esp_reset_reason()), static_cast<int>(esp_timer_get_time() / 1000));
    ESP_LOGI(kTag, "boot: free internal %u KB (largest block %u KB), PSRAM %u KB, app stack %u B left",
             static_cast<unsigned>(internal >> 10),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >> 10),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10), static_cast<unsigned>(stack));
  }
  if (internal < kLowInternal && !warned_heap) {
    ESP_LOGW(kTag, "internal RAM low: %u KB free", static_cast<unsigned>(internal >> 10));
  }
  warned_heap = internal < kLowInternal;
  if (stack < kLowStack && stack < warned_stack) {
    ESP_LOGW(kTag, "app stack nearly full: %u B were left at the deepest", static_cast<unsigned>(stack));
    warned_stack = stack;
  }
}

// Detects which controller this unit's panel has, then drives it over SPI.
class Display : public vellum::DisplayPanel {
 public:
  void init() {
    const epd::Pins pins{x4pro::kEpdSclk, x4pro::kEpdMosi, x4pro::kEpdCs,
                         x4pro::kEpdDc,   x4pro::kEpdRst,  x4pro::kEpdBusy};
    const epd::ProbeResult probe = epd::probe(pins);
    switch (probe.chip) {
      case epd::Chip::Ssd1677:
        controller_ = &epd::ssd1677(x4pro::kPanelWidth, x4pro::kPanelHeight);
        break;
      case epd::Chip::Uc8179:
        controller_ = &epd::uc8179(x4pro::kPanelWidth, x4pro::kPanelHeight);
        break;
      case epd::Chip::Uc8279:
        controller_ = &epd::uc8279(x4pro::kPanelWidth, x4pro::kPanelHeight);
        break;
    }
    ESP_ERROR_CHECK(bus_.init(pins, controller_->spi_hz()) ? ESP_OK : ESP_FAIL);
    controller_->begin(bus_);
    ESP_LOGI(kTag, "display controller %s", controller_->name());
    g_boot.controller = controller_->name();
  }

  int width() const override { return x4pro::kPanelWidth; }
  int height() const override { return x4pro::kPanelHeight; }

  // The controllers refresh the whole panel; `dirty` is not used yet.
  void present(const vellum::Framebuffer& fb, vellum::RefreshMode mode, vellum::Rect) override {
    const int64_t start = esp_timer_get_time();
    const bool full = mode == vellum::RefreshMode::Full;
    controller_->display(bus_, fb.data(), full ? epd::Mode::Full : epd::Mode::Fast, /*power_off=*/true);
    ESP_LOGI(kTag, "%s refresh %d ms", full ? "full" : "fast", static_cast<int>((esp_timer_get_time() - start) / 1000));
  }

 private:
  epd::Bus bus_;
  epd::Controller* controller_ = nullptr;
};

// Buttons and the touch controller raise GPIO interrupts; the waiting task
// debounces buttons and reads the touch controller. While a finger or the
// Home pad is down the touch controller is also polled, so a lost interrupt
// never leaves a press stuck.
//
// The CPU runs at full speed while the app works on an event and drops to
// the lowest speed (power management, see init_power_management) while it
// waits here for the next one.
class Input : public vellum::InputSource {
 public:
  void init() {
    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "app", &busy_) == ESP_OK) esp_pm_lock_acquire(busy_);
    queue_ = xQueueCreate(16, sizeof(uint32_t));
    gpio_config_t cfg = {};
    for (const Pin& p : kPins) cfg.pin_bit_mask |= 1ULL << p.gpio;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    for (uint32_t i = 0; i < kCount; ++i) {
      ESP_ERROR_CHECK(gpio_isr_handler_add(static_cast<gpio_num_t>(kPins[i].gpio), on_edge,
                                           reinterpret_cast<void*>(static_cast<uintptr_t>(i))));
    }
  }

  // Call after the I2C bus is up and the touch rail is powered.
  void init_touch(i2c_master_bus_handle_t i2c) {
    // Power the controller afresh so it starts from a known state (the rail
    // may have been held off through deep sleep), and let it settle.
    const auto power = static_cast<gpio_num_t>(x4pro::kTouchPowerN);
    gpio_set_level(power, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(power, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!touch_.attach(i2c, x4pro::kI2cHz, x4pro::kTouchRst, x4pro::kTouchInt)) {
      ESP_LOGW(kTag, "touch controller not found; buttons only");
      // What does answer on the bus, for the device check.
      for (uint16_t a = 0x08; a < 0x78 && bus_found_ < kBusSeen; ++a) {
        if (i2c_master_probe(i2c, a, 20) == ESP_OK) bus_seen_[bus_found_++] = static_cast<uint8_t>(a);
      }
      return;
    }
    have_touch_ = true;
    const auto irq = static_cast<gpio_num_t>(x4pro::kTouchInt);
    gpio_set_intr_type(irq, GPIO_INTR_ANYEDGE);
    ESP_ERROR_CHECK(gpio_isr_handler_add(irq, on_edge, reinterpret_cast<void*>(static_cast<uintptr_t>(kTouchSource))));
  }

  // Before deep sleep: the touch controller to its lowest power.
  void sleep_touch() {
    if (have_touch_) touch_.sleep();
  }

  // Wakes the waiting task with an InputEvent::Type::Transfer. Safe from any task.
  static void post_transfer() {
    const uint32_t i = kTransferSource;
    if (queue_ != nullptr) xQueueSend(queue_, &i, 0);
  }

  // Wakes it with an InputEvent::Type::Peer, at most one waiting at a time so
  // packets never crowd button presses out of the queue. Safe from any task.
  static void post_peer() {
    const uint32_t i = kPeerSource;
    if (queue_ != nullptr && !peer_pending_.exchange(true)) xQueueSend(queue_, &i, 0);
  }

  uint32_t now_ms() override { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

  bool touch_status(char* out, size_t size) override {
    if (!have_touch_) {
      int n = std::snprintf(out, size, "Not found. Bus:");
      for (int i = 0; i < bus_found_ && n > 0 && static_cast<size_t>(n) < size; ++i) {
        n += std::snprintf(out + n, size - n, " %02X", bus_seen_[i]);
      }
      if (bus_found_ == 0 && n > 0 && static_cast<size_t>(n) < size) std::snprintf(out + n, size - n, " none");
      return true;
    }
    std::snprintf(out, size, "%02X v%d %dx%d · irq %s · %lu ok %lu err · %02X", touch_.address(),
                  touch_.config_version(), touch_.max_x(), touch_.max_y(), touch_irq_seen_ ? "yes" : "no",
                  static_cast<unsigned long>(touch_.reports()), static_cast<unsigned long>(touch_.failures()),
                  touch_.last_status());
    return true;
  }

  bool wait(vellum::InputEvent& out, uint32_t timeout_ms) override {
    log_memory(!summary_logged_);
    summary_logged_ = true;
    if (pop(out)) return true;
    const bool held = touching_ || home_;
    // Until the touch controller's interrupt has been seen once, it is also
    // read every kNoIrqPollMs, in case its interrupt line is not wired as
    // expected. The app still sees one wait of timeout_ms.
    const bool no_irq = have_touch_ && !touch_irq_seen_ && !held;
    const uint32_t start = now_ms();
    uint32_t i;
    BaseType_t got;
    for (;;) {
      const uint32_t spent = now_ms() - start;
      uint32_t wait_ms = held ? kPollMs : timeout_ms - (spent < timeout_ms ? spent : timeout_ms);
      if (no_irq && wait_ms > kNoIrqPollMs) wait_ms = kNoIrqPollMs;
      if (busy_ != nullptr) esp_pm_lock_release(busy_);
      got = xQueueReceive(queue_, &i, pdMS_TO_TICKS(wait_ms));
      if (busy_ != nullptr) esp_pm_lock_acquire(busy_);
      if (got == pdTRUE || !no_irq) break;
      read_touch();
      if (pop(out)) return true;
      if (!no_irq_logged_ && now_ms() > 20000) {
        no_irq_logged_ = true;
        ESP_LOGW(kTag, "no touch interrupt yet; reading the touch controller every %lu ms",
                 static_cast<unsigned long>(kNoIrqPollMs));
      }
      if (now_ms() - start >= timeout_ms) return false;
    }
    if (got != pdTRUE) {
      if (!held) return false;
      i = kTouchSource;
    } else if (i == kTouchSource && !touch_irq_seen_) {
      touch_irq_seen_ = true;
      ESP_LOGI(kTag, "touch interrupt seen");
    }
    if (i == kTransferSource) {
      out.type = vellum::InputEvent::Type::Transfer;
      return true;
    }
    if (i == kPeerSource) {
      peer_pending_ = false;  // the app takes every packet waiting
      out.type = vellum::InputEvent::Type::Peer;
      return true;
    }
    if (i == kTouchSource) {
      read_touch();
      if (!pop(out)) out.type = vellum::InputEvent::Type::None;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kDebounceMs));
    const bool down = gpio_get_level(static_cast<gpio_num_t>(kPins[i].gpio)) == 0;
    if (down == pressed_[i]) {  // bounce: no change once settled
      out.type = vellum::InputEvent::Type::None;
      return true;
    }
    pressed_[i] = down;
    out.type = down ? vellum::InputEvent::Type::KeyDown : vellum::InputEvent::Type::KeyUp;
    out.key = kPins[i].key;
    return true;
  }

 private:
  struct Pin {
    int gpio;
    vellum::Key key;
  };
  static constexpr uint32_t kCount = 3;
  static constexpr Pin kPins[kCount] = {
      {x4pro::kBtnPrev, vellum::Key::Prev},
      {x4pro::kBtnNext, vellum::Key::Next},
      {x4pro::kBtnPower, vellum::Key::Power},
  };
  static constexpr uint32_t kTouchSource = kCount;
  static constexpr uint32_t kTransferSource = kCount + 1;
  static constexpr uint32_t kPeerSource = kCount + 2;
  static inline std::atomic<bool> peer_pending_{false};
  static constexpr uint32_t kPollMs = 25;
  static constexpr uint32_t kNoIrqPollMs = 100;

  static void IRAM_ATTR on_edge(void* arg) {
    const uint32_t i = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(queue_, &i, &woken);  // a full queue drops the edge; polling covers it
    if (woken) portYIELD_FROM_ISR();
  }

  // Turns the controller's newest report into Home key and touch events.
  void read_touch() {
    Gt911::Report r;
    if (!have_touch_ || !touch_.read(r)) return;
    using T = vellum::InputEvent::Type;
    if (r.home != home_) {
      home_ = r.home;
      vellum::InputEvent ev;
      ev.type = r.home ? T::KeyDown : T::KeyUp;
      ev.key = vellum::Key::Home;
      push(ev);
    }
    if (r.touching) {
      int x = x4pro::kTouchSwapXY ? r.y : r.x;
      int y = x4pro::kTouchSwapXY ? r.x : r.y;
      if (x4pro::kTouchFlipX) x = x4pro::kPanelWidth - 1 - x;
      if (x4pro::kTouchFlipY) y = x4pro::kPanelHeight - 1 - y;
      vellum::InputEvent ev;
      ev.type = touching_ ? T::TouchMove : T::TouchDown;
      ev.ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);  // tells a held finger from a tap
      ev.x = static_cast<int16_t>(x < 0 ? 0 : x >= x4pro::kPanelWidth ? x4pro::kPanelWidth - 1 : x);
      ev.y = static_cast<int16_t>(y < 0 ? 0 : y >= x4pro::kPanelHeight ? x4pro::kPanelHeight - 1 : y);
      if (!touching_) ESP_LOGI(kTag, "touch raw %d,%d -> panel %d,%d", r.x, r.y, ev.x, ev.y);
      touching_ = true;
      last_ = ev;
      push(ev);
    } else if (touching_) {
      touching_ = false;
      vellum::InputEvent ev = last_;  // lift where the finger last was
      ev.type = T::TouchUp;
      ev.ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
      push(ev);
    }
  }

  void push(const vellum::InputEvent& ev) {
    if (count_ < kPending) pending_[(head_ + count_++) % kPending] = ev;
  }
  bool pop(vellum::InputEvent& out) {
    if (count_ == 0) return false;
    out = pending_[head_];
    head_ = (head_ + 1) % kPending;
    --count_;
    return true;
  }

  static inline QueueHandle_t queue_ = nullptr;
  esp_pm_lock_handle_t busy_ = nullptr;  // held while the app works
  bool summary_logged_ = false;
  bool pressed_[kCount] = {};
  Gt911 touch_;
  bool have_touch_ = false;
  static constexpr int kBusSeen = 8;
  uint8_t bus_seen_[kBusSeen] = {};  // I2C addresses that answered, when the touch controller did not
  int bus_found_ = 0;
  bool touch_irq_seen_ = false;
  bool no_irq_logged_ = false;
  bool touching_ = false;
  bool home_ = false;
  vellum::InputEvent last_;
  static constexpr int kPending = 4;
  vellum::InputEvent pending_[kPending];
  int head_ = 0;
  int count_ = 0;
};

Input* g_input = nullptr;  // for enter_deep_sleep
void enter_deep_sleep(uint32_t wake_after_s);

// Charge level from the CW2017 gauge; charging state from the charger's status pin.
class Power : public vellum::Power {
 public:
  void init(i2c_master_bus_handle_t i2c) {
    if (!gauge_.attach(i2c, x4pro::kI2cHz)) ESP_LOGW(kTag, "battery gauge not added");
  }

  int battery_percent() override {
    // The level moves slowly; reading it at most once a minute keeps the bus quiet.
    const int64_t now = esp_timer_get_time();
    if (cached_ < 0 || now - read_at_ >= kCacheUs) {
      cached_ = gauge_.percent();
      read_at_ = now;
    }
    return cached_;
  }

  bool charging() override { return gpio_get_level(static_cast<gpio_num_t>(x4pro::kChargeStatus)) == 1; }

  void sleep() override { enter_deep_sleep(wake_after_s_); }
  void restart() override { esp_restart(); }
  void set_wake_timer(uint32_t seconds) override { wake_after_s_ = seconds; }
  bool woke_by_timer() override { return woke_by_timer_; }
  bool after_crash() override { return after_crash_; }

 private:
  static constexpr int64_t kCacheUs = 60LL * 1000 * 1000;
  Cw2017 gauge_;
  int cached_ = -1;
  int64_t read_at_ = 0;
  uint32_t wake_after_s_ = 0;
  // Read once at start: later light sleeps would change the answer.
  const bool woke_by_timer_ = (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER)) != 0;
  const bool after_crash_ = ended_in_crash(esp_reset_reason());
};

// Two LEDC channels mixed for brightness and colour temperature.
class Frontlight : public vellum::Frontlight {
 public:
  void init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = x4pro::kLightFreqHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    configure(LEDC_CHANNEL_0, x4pro::kLightCool);
    configure(LEDC_CHANNEL_1, x4pro::kLightWarm);
  }

  void set(uint8_t level, uint8_t warmth) override {
    // Perceptual curve: squared so low settings stay genuinely dim.
    const uint32_t total = (uint32_t{level} * level * kMaxDuty) / (255u * 255u);
    const uint32_t warm = total * warmth / 255u;
    duty(LEDC_CHANNEL_0, total - warm);
    duty(LEDC_CHANNEL_1, warm);
  }

 private:
  static constexpr uint32_t kMaxDuty = (1u << 10) - 1;

  static void configure(ledc_channel_t ch, int gpio) {
    ledc_channel_config_t c = {};
    c.gpio_num = gpio;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = ch;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&c));
  }
  static void duty(ledc_channel_t ch, uint32_t d) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, d);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
  }
};

// Deep sleep until the Power button is pressed (or `wake_after_s` passes,
// when not 0); waking restarts the firmware. The e-ink panel keeps showing
// the sleep screen with no power at all.
void enter_deep_sleep(uint32_t wake_after_s) {
  // Reaching sleep normally proves a new version works: keep it.
  esp_ota_mark_app_valid_cancel_rollback();

  const auto power_btn = static_cast<gpio_num_t>(x4pro::kBtnPower);
  // Wait for the button that sent us here to be let go, or it would wake us at once.
  while (gpio_get_level(power_btn) == 0) vTaskDelay(pdMS_TO_TICKS(10));
  vTaskDelay(pdMS_TO_TICKS(50));

  // Everything off and held off while asleep: frontlight, touch, SD card.
  // The display controller already powered down after its last refresh.
  if (g_input != nullptr) g_input->sleep_touch();
  for (int pin : {x4pro::kLightCool, x4pro::kLightWarm}) {
    const auto g = static_cast<gpio_num_t>(pin);
    gpio_set_direction(g, GPIO_MODE_OUTPUT);
    gpio_set_level(g, 0);
    gpio_hold_en(g);
  }
  for (int pin : {x4pro::kTouchPowerN, x4pro::kSdPowerN}) {  // active low: high is off
    const auto g = static_cast<gpio_num_t>(pin);
    gpio_set_level(g, 1);
    gpio_hold_en(g);
  }
  gpio_hold_en(static_cast<gpio_num_t>(x4pro::kPeripheralRail));
  gpio_deep_sleep_hold_en();

  rtc_gpio_pullup_en(power_btn);
  rtc_gpio_pulldown_dis(power_btn);
  esp_sleep_enable_ext0_wakeup(power_btn, 0);
  if (wake_after_s > 0) esp_sleep_enable_timer_wakeup(uint64_t{wake_after_s} * 1000 * 1000);
  ESP_LOGI(kTag, "deep sleep (timer %lu s)", static_cast<unsigned long>(wake_after_s));
  esp_deep_sleep_start();
}

void init_power_rails() {
  // Pins held through deep sleep stay frozen until released here.
  gpio_deep_sleep_hold_dis();
  for (int pin :
       {x4pro::kLightCool, x4pro::kLightWarm, x4pro::kTouchPowerN, x4pro::kSdPowerN, x4pro::kPeripheralRail}) {
    gpio_hold_dis(static_cast<gpio_num_t>(pin));
  }
  rtc_gpio_deinit(static_cast<gpio_num_t>(x4pro::kBtnPower));  // back to a normal GPIO for the button

  gpio_config_t out = {};
  out.pin_bit_mask = (1ULL << x4pro::kPeripheralRail) | (1ULL << x4pro::kTouchPowerN) | (1ULL << x4pro::kSdPowerN);
  out.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&out));
  gpio_set_level(static_cast<gpio_num_t>(x4pro::kPeripheralRail), 1);
  // Touch is powered from the start (it also carries the Home pad); the SD
  // card is powered when it is mounted. Both enables are active low.
  gpio_set_level(static_cast<gpio_num_t>(x4pro::kTouchPowerN), 0);
  gpio_set_level(static_cast<gpio_num_t>(x4pro::kSdPowerN), 1);

  gpio_config_t in = {};
  in.pin_bit_mask = 1ULL << x4pro::kChargeStatus;
  in.mode = GPIO_MODE_INPUT;
  ESP_ERROR_CHECK(gpio_config(&in));
}

// Battery while awake: dynamic frequency scaling between 240 MHz (while the
// app or the radio works: they hold power management locks) and 80 MHz. 80,
// not 40: below it the APB clock slows, and with it the front light's PWM.
// Automatic light sleep would save more, but the buttons, the touch
// interrupt and the front light must first be set up to work through it
// (docs/checkup.md).
void init_power_management() {
  esp_pm_config_t pm = {};
  pm.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  pm.min_freq_mhz = 80;
  pm.light_sleep_enable = false;
  const esp_err_t err = esp_pm_configure(&pm);
  if (err != ESP_OK) ESP_LOGW(kTag, "power management not set: %s", esp_err_to_name(err));
}

// Shared by the touch controller, the fuel gauge and the clock.
i2c_master_bus_handle_t init_i2c() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = static_cast<gpio_num_t>(x4pro::kI2cSda);
  cfg.scl_io_num = static_cast<gpio_num_t>(x4pro::kI2cScl);
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = 1;
  i2c_master_bus_handle_t bus = nullptr;
  ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
  return bus;
}

// After an update the new version runs on trial: if it restarts before this
// timer fires (a crash, a hang caught by the watchdog), the bootloader goes
// back to the previous version. A minute of running normally keeps it.
void keep_this_version_once_settled() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return;
  ESP_LOGI(kTag, "new version on trial");
  static esp_timer_handle_t timer = nullptr;
  esp_timer_create_args_t args = {};
  args.callback = [](void*) {
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(kTag, "new version kept");
  };
  args.name = "keep_version";
  if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_once(timer, 60LL * 1000 * 1000);
}

}  // namespace

vellum::Board& board_x4pro_init() {
  static Display display;
  static Input input;
  static Power power;
  static Frontlight light;
  static Bm8563 clock;

  init_power_rails();
  init_power_management();
  ESP_ERROR_CHECK(gpio_install_isr_service(0));  // buttons, touch and display BUSY
  input.init();
  light.init();
  const i2c_master_bus_handle_t i2c = init_i2c();
  power.init(i2c);
  if (!clock.attach(i2c, x4pro::kI2cHz)) ESP_LOGW(kTag, "clock not added");
  input.init_touch(i2c);
  g_input = &input;
  display.init();

  // Internal, DMA-capable RAM so the panel driver can stream it over SPI.
  auto* fb = static_cast<uint8_t*>(heap_caps_malloc(
      vellum::Framebuffer::bytes_for(x4pro::kPanelWidth, x4pro::kPanelHeight), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  assert(fb);

  // Portrait reading orientation; the exact rotation is to be confirmed on hardware.
  static vellum::StdioStorage card(kSdMountPoint);
  const bool have_card = mount_sd_card();
  g_boot.card = have_card;
  if (ended_in_crash(esp_reset_reason())) {
    ESP_LOGW(kTag,
             "the last run crashed (%s): starting at home with the built-in font; "
             "`idf.py coredump-info` shows where",
             reset_name(esp_reset_reason()));
  }

  // Wi-Fi stays off until "Add books" starts it.
  static WifiTransfer transfer(kSdMountPoint, Input::post_transfer, &clock);
  static EspNowPeers peers(Input::post_peer);

  static vellum::Board board{
      "XTEINK X4 Pro",        display, input,     power, &light, &clock, have_card ? &card : nullptr,
      vellum::Rotation::R270, fb,      &transfer, &peers};
  board.version = esp_app_get_description()->version;
  board.memory = memory_info;
  static OtaUpdater updater;
  board.updater = &updater;
  static HttpNetwork network;
  board.network = &network;
  keep_this_version_once_settled();
  ESP_LOGI(kTag, "board ready");
  return board;
}

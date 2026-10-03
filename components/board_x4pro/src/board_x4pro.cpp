#include "board_x4pro.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

constexpr const char* kTag = "x4pro";
constexpr int kDebounceMs = 10;

// Placeholder until the panel driver lands: logs what would be refreshed.
class PendingDisplay : public vellum::DisplayPanel {
 public:
  int width() const override { return x4pro::kPanelWidth; }
  int height() const override { return x4pro::kPanelHeight; }
  void present(const vellum::Framebuffer&, vellum::RefreshMode mode, vellum::Rect d) override {
    ESP_LOGI(kTag, "present %s %d,%d %dx%d (panel driver not yet implemented)",
             mode == vellum::RefreshMode::Full ? "full" : "fast", d.x, d.y, d.w, d.h);
  }
};

// Buttons raise a GPIO interrupt; the waiting task debounces and reads the level.
class Buttons : public vellum::InputSource {
 public:
  void init() {
    queue_ = xQueueCreate(16, sizeof(uint32_t));
    gpio_config_t cfg = {};
    for (const Pin& p : kPins) cfg.pin_bit_mask |= 1ULL << p.gpio;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (uint32_t i = 0; i < kCount; ++i) {
      ESP_ERROR_CHECK(gpio_isr_handler_add(static_cast<gpio_num_t>(kPins[i].gpio), on_edge,
                                           reinterpret_cast<void*>(static_cast<uintptr_t>(i))));
    }
  }

  bool wait(vellum::InputEvent& out, uint32_t timeout_ms) override {
    uint32_t i;
    if (xQueueReceive(queue_, &i, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
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

  static void IRAM_ATTR on_edge(void* arg) {
    const uint32_t i = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(queue_, &i, &woken);
    if (woken) portYIELD_FROM_ISR();
  }

  static inline QueueHandle_t queue_ = nullptr;
  bool pressed_[kCount] = {};
};

// Placeholder until the CW2017 fuel gauge driver lands.
class PendingPower : public vellum::Power {
 public:
  int battery_percent() override { return -1; }
  bool charging() override { return gpio_get_level(static_cast<gpio_num_t>(x4pro::kChargeStatus)) == 1; }
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

void init_power_rails() {
  gpio_config_t out = {};
  out.pin_bit_mask = (1ULL << x4pro::kPeripheralRail) | (1ULL << x4pro::kTouchPowerN) | (1ULL << x4pro::kSdPowerN);
  out.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&out));
  gpio_set_level(static_cast<gpio_num_t>(x4pro::kPeripheralRail), 1);
  // Touch and SD stay powered off until their drivers exist (both active low).
  gpio_set_level(static_cast<gpio_num_t>(x4pro::kTouchPowerN), 1);
  gpio_set_level(static_cast<gpio_num_t>(x4pro::kSdPowerN), 1);

  gpio_config_t in = {};
  in.pin_bit_mask = 1ULL << x4pro::kChargeStatus;
  in.mode = GPIO_MODE_INPUT;
  ESP_ERROR_CHECK(gpio_config(&in));
}

}  // namespace

vellum::Board& board_x4pro_init() {
  static PendingDisplay display;
  static Buttons buttons;
  static PendingPower power;
  static Frontlight light;

  init_power_rails();
  buttons.init();
  light.init();

  // Internal, DMA-capable RAM so the panel driver can stream it over SPI.
  auto* fb = static_cast<uint8_t*>(heap_caps_malloc(
      vellum::Framebuffer::bytes_for(x4pro::kPanelWidth, x4pro::kPanelHeight), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  assert(fb);

  // Portrait reading orientation; the exact rotation is to be confirmed on hardware.
  static vellum::Board board{"XTEINK X4 Pro", display, buttons, power, &light, vellum::Rotation::R90, fb};
  ESP_LOGI(kTag, "board ready");
  return board;
}

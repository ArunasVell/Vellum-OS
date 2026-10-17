#include "epd_bus.h"

#include <cstring>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace epd {
namespace {
constexpr const char* kTag = "epd";

void set(int pin, int level) { gpio_set_level(static_cast<gpio_num_t>(pin), level); }
}  // namespace

bool Bus::init(const Pins& pins, uint32_t spi_hz) {
  pins_ = pins;

  gpio_config_t out = {};
  out.pin_bit_mask = (1ULL << pins.cs) | (1ULL << pins.dc) | (1ULL << pins.rst);
  out.mode = GPIO_MODE_OUTPUT;
  if (gpio_config(&out) != ESP_OK) return false;
  set(pins.cs, 1);
  set(pins.dc, 1);
  set(pins.rst, 1);

  // BUSY wakes the waiting task on every edge, so refresh waits sleep instead of polling.
  gpio_config_t in = {};
  in.pin_bit_mask = 1ULL << pins.busy;
  in.mode = GPIO_MODE_INPUT;
  in.intr_type = GPIO_INTR_ANYEDGE;
  if (gpio_config(&in) != ESP_OK) return false;
  busy_edge_ = xSemaphoreCreateBinary();
  if (busy_edge_ == nullptr) return false;
  if (gpio_isr_handler_add(static_cast<gpio_num_t>(pins.busy), busy_isr, this) != ESP_OK) return false;

  chunk_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(4, kChunk, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (chunk_ == nullptr) return false;

  spi_bus_config_t bus = {};
  bus.mosi_io_num = pins.mosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = pins.sclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kChunk;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.mode = 0;
  dev.clock_speed_hz = static_cast<int>(spi_hz);
  dev.spics_io_num = -1;  // CS is driven by hand
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev, &dev_) != ESP_OK) return false;
  return true;
}

void Bus::reset(uint32_t settle_ms) {
  set(pins_.rst, 1);
  delay_ms(10);
  set(pins_.rst, 0);
  delay_ms(10);
  set(pins_.rst, 1);
  delay_ms(10);
  if (settle_ms) delay_ms(settle_ms);
}

void Bus::write(const uint8_t* d, size_t n) {
  spi_transaction_t t = {};
  t.length = n * 8;
  if (n <= 4) {
    t.flags = SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, d, n);
  } else {
    t.tx_buffer = d;
  }
  ESP_ERROR_CHECK(spi_device_polling_transmit(dev_, &t));
}

void Bus::cmd(uint8_t c) {
  set(pins_.dc, 0);
  set(pins_.cs, 0);
  write(&c, 1);
  set(pins_.cs, 1);
  set(pins_.dc, 1);
}

void Bus::data(uint8_t d) {
  set(pins_.dc, 1);
  set(pins_.cs, 0);
  write(&d, 1);
  set(pins_.cs, 1);
}

void Bus::cmd_data(uint8_t c, std::initializer_list<uint8_t> d) {
  cmd(c);
  for (uint8_t b : d) data(b);
}

void Bus::stream_begin(uint8_t c) {
  cmd(c);
  set(pins_.dc, 1);
  set(pins_.cs, 0);
  fill_ = 0;
}

void Bus::stream_bytes(const uint8_t* src, size_t n, bool invert) {
  while (n > 0) {
    const size_t take = (kChunk - fill_) < n ? (kChunk - fill_) : n;
    uint8_t* dst = chunk_ + fill_;
    if (invert) {
      for (size_t i = 0; i < take; ++i) dst[i] = static_cast<uint8_t>(~src[i]);
    } else {
      memcpy(dst, src, take);
    }
    fill_ += take;
    src += take;
    n -= take;
    if (fill_ == kChunk) flush();
  }
}

void Bus::stream_fill(uint8_t value, size_t n) {
  while (n > 0) {
    const size_t take = (kChunk - fill_) < n ? (kChunk - fill_) : n;
    memset(chunk_ + fill_, value, take);
    fill_ += take;
    n -= take;
    if (fill_ == kChunk) flush();
  }
}

void Bus::flush() {
  if (fill_ == 0) return;
  write(chunk_, fill_);
  fill_ = 0;
}

void Bus::stream_end() {
  flush();
  set(pins_.cs, 1);
}

int Bus::busy_level() const { return gpio_get_level(static_cast<gpio_num_t>(pins_.busy)); }

bool Bus::wait_for(int level, uint32_t timeout_ms, bool quiet) {
  const int64_t deadline = esp_timer_get_time() + int64_t{timeout_ms} * 1000;
  xSemaphoreTake(busy_edge_, 0);  // drop a stale edge
  if (busy_level() == level) return true;
  if (relax_ != nullptr) esp_pm_lock_release(relax_);
  bool reached = true;
  while (busy_level() != level) {
    if (esp_timer_get_time() >= deadline) {
      if (!quiet) ESP_LOGW(kTag, "BUSY did not reach %d within %u ms", level, static_cast<unsigned>(timeout_ms));
      reached = false;
      break;
    }
    // Woken by the next edge; the short cap covers an edge lost between the check and the take.
    xSemaphoreTake(busy_edge_, pdMS_TO_TICKS(10));
  }
  if (relax_ != nullptr) esp_pm_lock_acquire(relax_);
  return reached;
}

void Bus::delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1); }

void IRAM_ATTR Bus::busy_isr(void* arg) {
  auto* self = static_cast<Bus*>(arg);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(self->busy_edge_, &woken);
  if (woken) portYIELD_FROM_ISR();
}

}  // namespace epd

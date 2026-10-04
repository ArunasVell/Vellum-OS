#include "sd_card.h"

#include "board_x4pro.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

namespace {

constexpr const char* kTag = "sd";
constexpr int kAttempts = 3;

}  // namespace

// The mount sequence follows FreeInk's bring-up notes (MIT): power-cycle the
// slot through its active-low enable before every attempt, retry the whole
// mount (which includes real sector reads), and leave the enable low after.
bool mount_sd_card() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_1;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = static_cast<gpio_num_t>(x4pro::kSdClk);
  slot.cmd = static_cast<gpio_num_t>(x4pro::kSdCmd);
  slot.d0 = static_cast<gpio_num_t>(x4pro::kSdD0);
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mount = {};
  mount.format_if_mount_failed = false;
  mount.max_files = 6;
  mount.allocation_unit_size = 16 * 1024;

  const auto power = static_cast<gpio_num_t>(x4pro::kSdPowerN);
  for (int attempt = 1; attempt <= kAttempts; ++attempt) {
    gpio_set_level(power, 1);  // off
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_level(power, 0);  // on, and it stays on
    vTaskDelay(pdMS_TO_TICKS(120));
    sdmmc_card_t* card = nullptr;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(kSdMountPoint, &host, &slot, &mount, &card);
    if (err == ESP_OK) {
      ESP_LOGI(kTag, "card mounted: %s, %llu MB", card->cid.name,
               static_cast<unsigned long long>(card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
      return true;
    }
    ESP_LOGW(kTag, "mount attempt %d failed: %s", attempt, esp_err_to_name(err));
  }
  gpio_set_level(power, 1);  // no usable card: keep the slot unpowered
  return false;
}

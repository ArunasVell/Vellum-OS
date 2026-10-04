#include "wifi_common.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr const char* kNamespace = "vellum";
}  // namespace

bool wifi_base_init() {
  static bool done = false;
  if (done) return true;
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK || esp_netif_init() != ESP_OK) return false;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
  done = true;
  return true;
}

bool home_wifi_load(char* ssid, size_t ssid_size, char* password, size_t password_size) {
  if (ssid_size > 0) ssid[0] = '\0';
  if (password_size > 0) password[0] = '\0';
  if (!wifi_base_init()) return false;
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  size_t n = ssid_size;
  bool ok = nvs_get_str(h, "ssid", ssid, &n) == ESP_OK && ssid[0] != '\0';
  n = password_size;
  if (ok && nvs_get_str(h, "pass", password, &n) != ESP_OK) password[0] = '\0';
  nvs_close(h);
  return ok;
}

bool home_wifi_save(const char* ssid, const char* password) {
  if (!wifi_base_init()) return false;
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok =
      nvs_set_str(h, "ssid", ssid) == ESP_OK && nvs_set_str(h, "pass", password) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

#include "wifi_common.h"

#include <sys/time.h>

#include <cstring>
#include <ctime>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr const char* kNamespace = "vellum";
constexpr const char* kTag = "home-wifi";
constexpr int kJoinTimeoutMs = 20000;
constexpr int kPollMs = 100;  // how often a join asks whether it was cancelled
constexpr EventBits_t kGotIp = BIT0;
constexpr EventBits_t kGaveUp = BIT1;
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

vellum::UpdateResult HomeWifi::join(CancelFn cancelled, void* ctx) {
  if (joined_) return vellum::UpdateResult::UpToDate;
  char ssid[33];
  char password[65];
  if (!home_wifi_load(ssid, sizeof(ssid), password, sizeof(password))) return vellum::UpdateResult::NoNetwork;
  static esp_netif_t* netif = nullptr;
  static EventGroupHandle_t events = nullptr;
  if (netif == nullptr) {
    netif = esp_netif_create_default_wifi_sta();
    events = xEventGroupCreate();
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, events);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, events);
  }
  events_ = events;
  xEventGroupClearBits(events_, kGotIp | kGaveUp);
  if (awake_ == nullptr) esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "home-wifi", &awake_);
  if (awake_ != nullptr && !held_ && esp_pm_lock_acquire(awake_) == ESP_OK) held_ = true;

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = false;
  if (!on_) {
    if (esp_wifi_init(&init) != ESP_OK) return vellum::UpdateResult::NoWifi;
    on_ = true;
  }
  wifi_config_t cfg = {};
  std::memcpy(cfg.sta.ssid, ssid, std::strlen(ssid));
  std::memcpy(cfg.sta.password, password, std::strlen(password));
  cfg.sta.threshold.authmode = password[0] != '\0' ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK ||
      esp_wifi_start() != ESP_OK) {
    return vellum::UpdateResult::NoWifi;
  }
  // A few tries: home routers can be slow to answer a sleepy device. The
  // waits come in short slices, so a cancel is noticed at once.
  const auto stop = [&] { return cancelled != nullptr && cancelled(ctx); };
  for (int attempt = 0; attempt < 3; ++attempt) {
    xEventGroupClearBits(events_, kGotIp | kGaveUp);
    esp_wifi_connect();
    EventBits_t bits = 0;
    for (int waited = 0; waited < kJoinTimeoutMs && !(bits & (kGotIp | kGaveUp)); waited += kPollMs) {
      if (stop()) break;
      bits = xEventGroupWaitBits(events_, kGotIp | kGaveUp, pdTRUE, pdFALSE, pdMS_TO_TICKS(kPollMs));
    }
    if (bits & kGotIp) {
      ESP_LOGI(kTag, "joined %s", ssid);
      joined_ = true;
      if (clock_ != nullptr && !asking_time_) {
        // The answer comes on its own while the network stays up.
        esp_sntp_config_t time = {};
        time.wait_for_sync = true;
        time.start = true;
        time.sync_cb = on_time;
        time.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
        time.num_of_servers = 1;
        time.servers[0] = "pool.ntp.org";
        asking_time_ = esp_netif_sntp_init(&time) == ESP_OK;
      }
      return vellum::UpdateResult::UpToDate;
    }
    esp_wifi_disconnect();
    if (stop()) {
      ESP_LOGI(kTag, "join of %s cancelled", ssid);
      return vellum::UpdateResult::NoWifi;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  ESP_LOGW(kTag, "could not join %s", ssid);
  return vellum::UpdateResult::NoWifi;
}

void HomeWifi::leave() {
  joined_ = false;
  if (asking_time_) {
    esp_netif_sntp_deinit();
    asking_time_ = false;
  }
  if (on_) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    on_ = false;
  }
  if (held_) {
    esp_pm_lock_release(awake_);
    held_ = false;
  }
}

void HomeWifi::on_event(void* arg, esp_event_base_t base, int32_t, void*) {
  xEventGroupSetBits(static_cast<EventGroupHandle_t>(arg), base == IP_EVENT ? kGotIp : kGaveUp);
}

void HomeWifi::on_time(struct timeval* tv) {
  const time_t seconds = tv->tv_sec;
  std::tm utc{};
  if (clock_ == nullptr || gmtime_r(&seconds, &utc) == nullptr || utc.tm_year + 1900 < 2026) return;
  const vellum::DateTime t{static_cast<uint16_t>(utc.tm_year + 1900), static_cast<uint8_t>(utc.tm_mon + 1),
                           static_cast<uint8_t>(utc.tm_mday),         static_cast<uint8_t>(utc.tm_hour),
                           static_cast<uint8_t>(utc.tm_min),          static_cast<uint8_t>(utc.tm_sec)};
  const bool ok = clock_->set(t);
  ESP_LOGI(kTag, "clock %s from the time server: %02d:%02d UTC", ok ? "set" : "not set", utc.tm_hour, utc.tm_min);
}

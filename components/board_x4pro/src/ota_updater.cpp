#include "ota_updater.h"

#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "vellum/updates.h"
#include "wifi_common.h"

namespace {

constexpr const char* kTag = "updater";
// Where releases are published (CI's release job). The repository must be
// public for a reader to see them without signing in.
constexpr const char* kLatestRelease = "https://api.github.com/repos/ArunasVell/Vellum-OS/releases/latest";
constexpr size_t kAnswerBytes = 64 * 1024;  // the release answer lists its notes and assets

struct Answer {
  char* data;
  size_t len;
};

esp_err_t collect(esp_http_client_event_t* ev) {
  auto* a = static_cast<Answer*>(ev->user_data);
  if (ev->event_id == HTTP_EVENT_ON_DATA && a->len + ev->data_len < kAnswerBytes) {
    std::memcpy(a->data + a->len, ev->data, ev->data_len);
    a->len += ev->data_len;
  }
  return ESP_OK;
}

}  // namespace

void OtaUpdater::home_network(char* out, size_t size) {
  char password[65];
  home_wifi_load(out, size, password, sizeof(password));
}

vellum::UpdateResult OtaUpdater::check(const char* current, char* latest, size_t size) {
  HomeWifi wifi;
  const vellum::UpdateResult joined = wifi.join();
  if (joined != vellum::UpdateResult::UpToDate) return joined;

  static char* buffer = static_cast<char*>(heap_caps_malloc(kAnswerBytes, MALLOC_CAP_SPIRAM));
  if (buffer == nullptr) return vellum::UpdateResult::NoServer;
  Answer answer{buffer, 0};
  esp_http_client_config_t cfg = {};
  cfg.url = kLatestRelease;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.event_handler = collect;
  cfg.user_data = &answer;
  cfg.timeout_ms = 15000;
  cfg.buffer_size = 4096;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) return vellum::UpdateResult::NoServer;
  esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
  esp_http_client_set_header(client, "User-Agent", "Vellum");
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (err != ESP_OK || status != 200) {
    ESP_LOGW(kTag, "release server: %s, status %d", esp_err_to_name(err), status);
    return vellum::UpdateResult::NoServer;
  }
  answer.data[answer.len] = '\0';
  char version[32];
  if (!vellum::parse_release(answer.data, answer.len, version, sizeof(version), url_, sizeof(url_))) {
    ESP_LOGW(kTag, "no update file in the latest release");
    return vellum::UpdateResult::NoServer;
  }
  std::snprintf(latest, size, "%s", version);
  ESP_LOGI(kTag, "latest %s, running %s", version, current);
  return vellum::is_newer_version(version, current) ? vellum::UpdateResult::Available : vellum::UpdateResult::UpToDate;
}

vellum::UpdateResult OtaUpdater::install(ProgressFn progress, void* ctx) {
  if (url_[0] == '\0') {
    char latest[32];
    const vellum::UpdateResult r = check(esp_app_get_description()->version, latest, sizeof(latest));
    if (r != vellum::UpdateResult::Available)
      return r == vellum::UpdateResult::UpToDate ? vellum::UpdateResult::Failed : r;
  }
  HomeWifi wifi;
  const vellum::UpdateResult joined = wifi.join();
  if (joined != vellum::UpdateResult::UpToDate) return joined;

  esp_http_client_config_t http = {};
  http.url = url_;
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.timeout_ms = 15000;
  http.buffer_size = 4096;     // GitHub's download redirect carries a long address
  http.buffer_size_tx = 2048;  // and so does the request that follows it
  http.keep_alive_enable = true;
  esp_https_ota_config_t cfg = {};
  cfg.http_config = &http;
  esp_https_ota_handle_t ota = nullptr;
  if (esp_https_ota_begin(&cfg, &ota) != ESP_OK) {
    ESP_LOGW(kTag, "download did not start");
    return vellum::UpdateResult::NoServer;
  }
  // Only a Vellum build goes in.
  esp_app_desc_t desc;
  if (esp_https_ota_get_img_desc(ota, &desc) != ESP_OK ||
      std::strncmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)) != 0) {
    esp_https_ota_abort(ota);
    return vellum::UpdateResult::Failed;
  }
  const int total = esp_https_ota_get_image_size(ota);
  esp_err_t err;
  while ((err = esp_https_ota_perform(ota)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    if (progress != nullptr && total > 0) {
      progress(ctx, static_cast<int>(int64_t{esp_https_ota_get_image_len_read(ota)} * 100 / total));
    }
  }
  if (err != ESP_OK || !esp_https_ota_is_complete_data_received(ota)) {
    ESP_LOGW(kTag, "download broke off: %s", esp_err_to_name(err));
    esp_https_ota_abort(ota);
    return vellum::UpdateResult::Failed;
  }
  if (esp_https_ota_finish(ota) != ESP_OK) return vellum::UpdateResult::Failed;  // checks the image, sets the boot slot
  if (progress != nullptr) progress(ctx, 100);
  ESP_LOGI(kTag, "installed %s", desc.version);
  url_[0] = '\0';
  return vellum::UpdateResult::Installed;
}

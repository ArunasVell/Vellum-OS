#include "http_network.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char* kTag = "store";
constexpr uint32_t kYieldBytes = 32 * 1024;  // a pause this often lets the idle task (and its watchdog) run

struct Call {
  vellum::Network::BodyFn body;
  void* ctx;
  uint32_t got = 0;
  uint32_t since_yield = 0;
  bool stopped = false;
};

esp_err_t on_event(esp_http_client_event_t* ev) {
  auto& call = *static_cast<Call*>(ev->user_data);
  if (ev->event_id != HTTP_EVENT_ON_DATA || call.stopped) return ESP_OK;
  // A redirect's own body is not the answer.
  if (esp_http_client_get_status_code(ev->client) != 200) return ESP_OK;
  const int64_t length = esp_http_client_get_content_length(ev->client);
  const uint32_t total = length > 0 ? static_cast<uint32_t>(length) : 0;
  if (!call.body(call.ctx, static_cast<const uint8_t*>(ev->data), static_cast<uint32_t>(ev->data_len), total)) {
    call.stopped = true;
    esp_http_client_cancel_request(ev->client);
    return ESP_OK;
  }
  call.got += static_cast<uint32_t>(ev->data_len);
  call.since_yield += static_cast<uint32_t>(ev->data_len);
  if (call.since_yield >= kYieldBytes) {
    call.since_yield = 0;
    vTaskDelay(1);
  }
  return ESP_OK;
}

}  // namespace

vellum::NetResult HttpNetwork::get(const char* url, BodyFn body, void* ctx) {
  if (!wifi_.joined()) {
    const vellum::UpdateResult joined = wifi_.join();
    if (joined != vellum::UpdateResult::UpToDate) {
      wifi_.leave();  // radio off again
      return joined == vellum::UpdateResult::NoNetwork ? vellum::NetResult::NoNetwork : vellum::NetResult::NoWifi;
    }
  }
  Call call{body, ctx};
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.event_handler = on_event;
  cfg.user_data = &call;
  cfg.timeout_ms = 15000;
  cfg.buffer_size = 4096;     // Gutenberg's download redirects carry long addresses
  cfg.buffer_size_tx = 2048;  // and so do the requests that follow them
  cfg.keep_alive_enable = true;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) return vellum::NetResult::NoServer;
  esp_http_client_set_header(client, "User-Agent", "Vellum (e-reader)");
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  const int64_t length = esp_http_client_get_content_length(client);
  esp_http_client_cleanup(client);
  if (call.stopped) return vellum::NetResult::Stopped;
  if (err != ESP_OK || status != 200) {
    ESP_LOGW(kTag, "%s: %s, status %d", url, esp_err_to_name(err), status);
    if (call.got > 0) return vellum::NetResult::BrokeOff;
    if (err != ESP_OK) wifi_.leave();  // the network may have gone: join again next time
    return vellum::NetResult::NoServer;
  }
  if (length > 0 && call.got < length) return vellum::NetResult::BrokeOff;
  return vellum::NetResult::Ok;
}

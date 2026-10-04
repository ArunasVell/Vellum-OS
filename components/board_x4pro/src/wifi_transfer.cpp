#include "wifi_transfer.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "vellum/upload_page.h"
#include "wifi_common.h"

namespace {

constexpr const char* kTag = "transfer";
constexpr size_t kChunk = 16 * 1024;          // one receive and write
constexpr uint64_t kSpareBytes = 256 * 1024;  // keep this much of the card free
constexpr int kMaxDevices = 2;

}  // namespace

struct WifiTransfer::Impl {
  const char* root;
  NotifyFn notify;
  vellum::Clock* clock;
  SemaphoreHandle_t lock = nullptr;
  vellum::TransferStatus st;
  httpd_handle_t server = nullptr;
  esp_netif_t* netif = nullptr;
  esp_pm_lock_handle_t awake = nullptr;
  bool awake_held = false;
  bool wifi_on = false;
  char* buffer = nullptr;  // kChunk bytes, allocated once

  template <typename F>
  void update(F&& change) {
    xSemaphoreTake(lock, portMAX_DELAY);
    change(st);
    xSemaphoreGive(lock);
    notify();
  }

  void books_dir(char* out, size_t size) const { std::snprintf(out, size, "%s/books", root); }

  static void on_wifi_event(void* arg, esp_event_base_t, int32_t id, void*) {
    auto* self = static_cast<Impl*>(arg);
    if (id == WIFI_EVENT_AP_STACONNECTED) self->update([](vellum::TransferStatus& s) { ++s.devices; });
    if (id == WIFI_EVENT_AP_STADISCONNECTED) {
      self->update([](vellum::TransferStatus& s) { s.devices = s.devices > 0 ? s.devices - 1 : 0; });
    }
  }

  static esp_err_t get_page(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, vellum::kUploadPage, vellum::kUploadPageSize);
  }

  static esp_err_t get_setup(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, vellum::kSetupPage, vellum::kSetupPageSize);
  }

  // The home Wi-Fi for updates, from the setup page's form.
  static esp_err_t post_wifi(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char body[512];
    char ssid[33];
    char password[65];
    const int n = req->content_len < sizeof(body) ? httpd_req_recv(req, body, req->content_len) : -1;
    if (n <= 0 || !vellum::form_field(body, static_cast<size_t>(n), "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0' ||
        !vellum::form_field(body, static_cast<size_t>(n), "password", password, sizeof(password))) {
      return fail(req, "400 Bad Request", "Expected a network name and password.");
    }
    if (!home_wifi_save(ssid, password)) return fail(req, "500 Internal Server Error", "Could not save.");
    ESP_LOGI(kTag, "home Wi-Fi set to %s", ssid);
    self->update([](vellum::TransferStatus& s) { ++s.home_wifi_saved; });
    return httpd_resp_sendstr(req, "ok");
  }

  static esp_err_t get_books(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char dir_path[64];
    self->books_dir(dir_path, sizeof(dir_path));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    if (DIR* d = opendir(dir_path)) {
      bool first = true;
      while (dirent* e = readdir(d)) {
        const size_t n = std::strlen(e->d_name);
        if (e->d_name[0] == '.' || n < 5 || strcasecmp(e->d_name + n - 5, ".epub") != 0) continue;
        char path[384];
        std::snprintf(path, sizeof(path), "%s/%s", dir_path, e->d_name);
        struct stat sb;
        const long size = stat(path, &sb) == 0 ? static_cast<long>(sb.st_size) : 0;
        char item[512];
        size_t len = 0;
        len += std::snprintf(item, sizeof(item), "%s{\"name\":\"", first ? "" : ",");
        if (!vellum::json_escape(e->d_name, item, sizeof(item) - 40, len)) continue;
        std::snprintf(item + len, sizeof(item) - len, "\",\"size\":%ld}", size);
        httpd_resp_sendstr_chunk(req, item);
        first = false;
      }
      closedir(d);
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, nullptr);
  }

  static esp_err_t fail(httpd_req_t* req, const char* status, const char* text) {
    httpd_resp_set_status(req, status);
    return httpd_resp_sendstr(req, text);
  }

  static esp_err_t post_upload(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);

    // The file name, from ?name=...
    char query[512];
    char raw[384];
    char decoded[384];
    char name[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", raw, sizeof(raw)) != ESP_OK ||
        !vellum::url_decode(raw, std::strlen(raw), decoded, sizeof(decoded)) ||
        !vellum::safe_book_name(decoded, name, sizeof(name))) {
      return fail(req, "400 Bad Request", "Only EPUB books can be added.");
    }

    // Room on the card?
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(self->root, &total, &free_bytes) == ESP_OK &&
        static_cast<uint64_t>(req->content_len) + kSpareBytes > free_bytes) {
      self->update([](vellum::TransferStatus& s) { s.card_full = true; });
      return fail(req, "507 Insufficient Storage", "The card is full.");
    }

    char dir_path[64];
    self->books_dir(dir_path, sizeof(dir_path));
    mkdir(dir_path, 0775);
    char path[256];
    char part[262];
    std::snprintf(path, sizeof(path), "%s/%s", dir_path, name);
    std::snprintf(part, sizeof(part), "%s.part", path);

    self->update([&](vellum::TransferStatus& s) {
      std::snprintf(s.receiving, sizeof(s.receiving), "%s", name);
      s.card_full = false;
    });
    auto done = [&](bool ok) {
      self->update([&](vellum::TransferStatus& s) {
        s.receiving[0] = '\0';
        if (ok) {
          ++s.received;
          std::snprintf(s.last, sizeof(s.last), "%s", name);
        }
      });
    };

    std::FILE* f = std::fopen(part, "wb");
    if (f == nullptr) {
      done(false);
      return fail(req, "500 Internal Server Error", "Could not write to the card.");
    }
    setvbuf(f, nullptr, _IOFBF, 8192);
    size_t left = req->content_len;
    bool ok = true;
    while (left > 0) {
      const int n = httpd_req_recv(req, self->buffer, left < kChunk ? left : kChunk);
      if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (n <= 0 || std::fwrite(self->buffer, 1, n, f) != static_cast<size_t>(n)) {
        ok = false;
        break;
      }
      left -= n;
    }
    ok = std::fclose(f) == 0 && ok;
    if (ok) {
      std::remove(path);  // FAT cannot rename over an existing file
      ok = std::rename(part, path) == 0;
    }
    if (!ok) std::remove(part);
    done(ok);
    ESP_LOGI(kTag, "%s %s (%u bytes)", ok ? "saved" : "failed", name, static_cast<unsigned>(req->content_len));
    if (!ok) return fail(req, "500 Internal Server Error", "The book could not be saved.");
    return httpd_resp_sendstr(req, "ok");
  }

  static esp_err_t get_info(httpd_req_t* req) {
    char body[256];
    char ssid[33];
    char password[65];
    home_wifi_load(ssid, sizeof(ssid), password, sizeof(password));
    size_t len = 0;
    len += std::snprintf(body, sizeof(body), "{\"version\":\"");
    vellum::json_escape(esp_app_get_description()->version, body, 100, len);
    len += std::snprintf(body + len, sizeof(body) - len, "\",\"home_network\":\"");
    vellum::json_escape(ssid, body, sizeof(body) - 4, len);
    std::snprintf(body + len, sizeof(body) - len, "\"}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
  }

  // A Vellum build for this chip starts with the image header, one segment
  // header, then the app description naming the project.
  static constexpr size_t kHeadBytes =
      sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

  static bool is_vellum(const uint8_t* head, char* version, size_t size) {
    const auto* image = reinterpret_cast<const esp_image_header_t*>(head);
    const auto* app =
        reinterpret_cast<const esp_app_desc_t*>(head + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (image->magic != ESP_IMAGE_HEADER_MAGIC || image->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID ||
        app->magic_word != ESP_APP_DESC_MAGIC_WORD ||
        strncmp(app->project_name, esp_app_get_description()->project_name, sizeof(app->project_name)) != 0) {
      return false;
    }
    std::snprintf(version, size, "%.*s", static_cast<int>(sizeof(app->version)), app->version);
    return true;
  }

  // The new software goes into the spare app slot; the reader starts it on
  // the next restart and goes back to this one if it fails to start.
  static esp_err_t post_update(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    using U = vellum::TransferStatus::Update;
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    auto finish = [&](U result) { self->update([&](vellum::TransferStatus& s) { s.update = result; }); };
    if (slot == nullptr || req->content_len < kHeadBytes || req->content_len > slot->size) {
      finish(U::NotVellum);
      return fail(req, "400 Bad Request", "That file is not a Vellum update.");
    }
    self->update([](vellum::TransferStatus& s) {
      s.update = U::Receiving;
      s.update_percent = 0;
      s.update_version[0] = '\0';
    });

    esp_ota_handle_t ota = 0;
    bool begun = false;
    bool ok = true;
    U failure = U::Broken;
    size_t got = 0;
    int shown = 0;
    static uint8_t head[kHeadBytes];  // the first bytes, checked before anything is written
    while (got < req->content_len) {
      const size_t left = req->content_len - got;
      const int n = httpd_req_recv(req, self->buffer, left < kChunk ? left : kChunk);
      if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (n <= 0) {
        ok = false;
        break;
      }
      const uint8_t* data = reinterpret_cast<const uint8_t*>(self->buffer);
      size_t len = static_cast<size_t>(n);
      if (!begun) {
        const size_t take = std::min(len, kHeadBytes - got);
        std::memcpy(head + got, data, take);
        got += take;
        data += take;
        len -= take;
        if (got < kHeadBytes) continue;
        char version[32];
        if (!is_vellum(head, version, sizeof(version))) {
          ok = false;
          failure = U::NotVellum;
          break;
        }
        self->update([&](vellum::TransferStatus& s) {
          std::snprintf(s.update_version, sizeof(s.update_version), "%s", version);
        });
        if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK ||
            esp_ota_write(ota, head, kHeadBytes) != ESP_OK) {
          ok = false;
          break;
        }
        begun = true;
      }
      if (len > 0 && esp_ota_write(ota, data, len) != ESP_OK) {
        ok = false;
        break;
      }
      got += len;
      const int percent = static_cast<int>(uint64_t{got} * 100 / req->content_len);
      if (percent / 5 != shown / 5) {
        shown = percent;
        self->update([&](vellum::TransferStatus& s) { s.update_percent = percent; });
      }
    }
    if (begun) {
      ok = ok && esp_ota_end(ota) == ESP_OK && esp_ota_set_boot_partition(slot) == ESP_OK;  // end checks the image
      if (!ok) esp_ota_abort(ota);
    }
    ESP_LOGI(kTag, "update %s (%u bytes)", ok ? "installed" : "failed", static_cast<unsigned>(req->content_len));
    if (!ok) {
      finish(failure);
      return fail(req, "400 Bad Request",
                  failure == U::NotVellum ? "That file is not a Vellum update." : "The update did not arrive whole.");
    }
    // Answer first: the reader stops the network and restarts once told.
    const esp_err_t sent = httpd_resp_sendstr(req, "ok");
    finish(U::Installed);
    return sent;
  }

  static esp_err_t post_time(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char body[32];
    vellum::DateTime t;
    const int n = req->content_len < sizeof(body) ? httpd_req_recv(req, body, req->content_len) : -1;
    if (n <= 0 || !vellum::parse_datetime(body, static_cast<size_t>(n), t)) {
      return fail(req, "400 Bad Request", "Expected YYYY-MM-DD HH:MM:SS.");
    }
    if (self->clock == nullptr || !self->clock->set(t)) {
      return fail(req, "500 Internal Server Error", "The clock could not be set.");
    }
    ESP_LOGI(kTag, "clock set to %.*s", n, body);
    self->update([](vellum::TransferStatus&) {});  // redraw with the new time
    return httpd_resp_sendstr(req, "ok");
  }

  bool start_wifi(vellum::TransferInfo& info) {
    static bool once = false;
    if (!once) {
      if (!wifi_base_init()) return false;
      netif = esp_netif_create_default_wifi_ap();
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, this);
      once = true;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;  // nothing to remember between sessions
    if (esp_wifi_init(&init) != ESP_OK) return false;
    wifi_on = true;

    // A name that stays the same for this reader, and a fresh password each time.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(info.network, sizeof(info.network), "Vellum-%02X%02X", mac[4], mac[5]);
    std::snprintf(info.password, sizeof(info.password), "%08lu",
                  static_cast<unsigned long>(esp_random() % 100000000UL));
    std::snprintf(info.address, sizeof(info.address), "http://192.168.4.1");

    wifi_config_t cfg = {};
    std::memcpy(cfg.ap.ssid, info.network, std::strlen(info.network));
    cfg.ap.ssid_len = static_cast<uint8_t>(std::strlen(info.network));
    std::memcpy(cfg.ap.password, info.password, std::strlen(info.password));
    cfg.ap.channel = 6;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = kMaxDevices;
    cfg.ap.pmf_cfg.required = false;
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
      return false;
    }
    ESP_LOGI(kTag, "network %s up", info.network);
    return true;
  }

  bool start_server() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;  // FAT writes and JSON
    cfg.max_open_sockets = 4;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 10;
    if (httpd_start(&server, &cfg) != ESP_OK) return false;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = get_page, .user_ctx = this},
        {.uri = "/books", .method = HTTP_GET, .handler = get_books, .user_ctx = this},
        {.uri = "/upload", .method = HTTP_POST, .handler = post_upload, .user_ctx = this},
        {.uri = "/time", .method = HTTP_POST, .handler = post_time, .user_ctx = this},
        {.uri = "/info", .method = HTTP_GET, .handler = get_info, .user_ctx = this},
        {.uri = "/update", .method = HTTP_POST, .handler = post_update, .user_ctx = this},
        {.uri = "/setup", .method = HTTP_GET, .handler = get_setup, .user_ctx = this},
        {.uri = "/wifi", .method = HTTP_POST, .handler = post_wifi, .user_ctx = this},
    };
    for (const httpd_uri_t& r : routes) httpd_register_uri_handler(server, &r);
    return true;
  }
};

WifiTransfer::WifiTransfer(const char* card_root, NotifyFn notify, vellum::Clock* clock) : impl_(new Impl) {
  impl_->root = card_root;
  impl_->notify = notify;
  impl_->clock = clock;
  impl_->lock = xSemaphoreCreateMutex();
}

bool WifiTransfer::start(vellum::TransferInfo& info) {
  Impl& m = *impl_;
  stop();
  if (m.buffer == nullptr) m.buffer = static_cast<char*>(heap_caps_malloc(kChunk, MALLOC_CAP_DEFAULT));
  if (m.awake == nullptr) esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "transfer", &m.awake);
  if (m.buffer == nullptr) return false;
  xSemaphoreTake(m.lock, portMAX_DELAY);
  m.st = {};
  xSemaphoreGive(m.lock);
  if (m.awake != nullptr && esp_pm_lock_acquire(m.awake) == ESP_OK) m.awake_held = true;  // keep the radio responsive
  if (!m.start_wifi(info) || !m.start_server()) {
    ESP_LOGW(kTag, "could not start");
    stop();
    return false;
  }
  return true;
}

void WifiTransfer::stop() {
  Impl& m = *impl_;
  if (m.server != nullptr) {
    httpd_stop(m.server);
    m.server = nullptr;
  }
  if (m.wifi_on) {
    esp_wifi_stop();
    esp_wifi_deinit();  // frees the radio's memory until next time
    m.wifi_on = false;
  }
  if (m.awake_held) {
    esp_pm_lock_release(m.awake);
    m.awake_held = false;
  }
}

void WifiTransfer::status(vellum::TransferStatus& out) {
  xSemaphoreTake(impl_->lock, portMAX_DELAY);
  out = impl_->st;
  xSemaphoreGive(impl_->lock);
}

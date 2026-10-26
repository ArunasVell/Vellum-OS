#include "wifi_transfer.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now_peers.h"
#include "esp_ota_ops.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "vellum/highlights.h"
#include "vellum/library.h"
#include "vellum/sleep_pictures.h"
#include "vellum/stdio_storage.h"
#include "vellum/upload_page.h"
#include "wifi_common.h"

namespace {

constexpr const char* kTag = "transfer";
constexpr size_t kChunk = 16 * 1024;          // one receive and write
constexpr uint64_t kSpareBytes = 256 * 1024;  // keep this much of the card free
constexpr int kMaxDevices = 2;
// Receive timeouts (recv_wait_timeout each) in a row before an upload is
// given up: a phone that walked away never closes its connection, and a
// handler waiting for it forever would also hang httpd_stop(), and with it
// the app when Home closes the transfer.
constexpr int kMaxSilentWaits = 3;
constexpr const char* kWhatFits =
    "Only books (EPUB, FB2, TXT, HTML), fonts, dictionaries and pictures (JPG, PNG, BMP) can be added.";

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
  HomeWifi home;                // the home Wi-Fi, when the page is served there
  const char* fonts = nullptr;  // TransferInfo::fonts, for the page's Settings
  char* buffer = nullptr;       // kChunk bytes, allocated once
  // The highlights file, for the phone page's notes. Made the first time the
  // page asks; the server's own storage, so it never shares the app's.
  vellum::StdioStorage* card = nullptr;
  vellum::Highlights* highlights = nullptr;

  bool load_card() {
    if (card == nullptr) card = new (std::nothrow) vellum::StdioStorage(root);
    return card != nullptr;
  }

  bool load_highlights() {
    if (highlights == nullptr) highlights = new (std::nothrow) vellum::Highlights;
    if (!load_card() || highlights == nullptr) return false;
    highlights->load(*card);
    return true;
  }

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

  static esp_err_t get_books(httpd_req_t* req) { return list_files(req, "books", vellum::is_book_file); }

  // The sleep screen pictures, listed like the books.
  static esp_err_t get_sleep(httpd_req_t* req) {
    return list_files(req, vellum::SleepPictures::kFolder, vellum::is_picture_file);
  }

  // A folder's files that `wanted` accepts, as JSON: [{"name": ..., "size": ...}].
  static esp_err_t list_files(httpd_req_t* req, const char* folder, bool (*wanted)(const char*)) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char dir_path[64];
    std::snprintf(dir_path, sizeof(dir_path), "%s/%s", self->root, folder);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    if (DIR* d = opendir(dir_path)) {
      bool first = true;
      while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.' || !wanted(e->d_name)) continue;
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
        !vellum::url_decode(raw, std::strlen(raw), decoded, sizeof(decoded))) {
      return fail(req, "400 Bad Request", kWhatFits);
    }
    const bool font = vellum::safe_font_name(decoded, name, sizeof(name));
    const bool dictionary = !font && vellum::safe_dictionary_name(decoded, name, sizeof(name));
    const bool picture = !font && !dictionary && vellum::safe_picture_name(decoded, name, sizeof(name));
    if (!font && !dictionary && !picture && !vellum::safe_book_name(decoded, name, sizeof(name))) {
      return fail(req, "400 Bad Request", kWhatFits);
    }

    // Room on the card?
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(self->root, &total, &free_bytes) == ESP_OK &&
        static_cast<uint64_t>(req->content_len) + kSpareBytes > free_bytes) {
      self->update([](vellum::TransferStatus& s) { s.card_full = true; });
      return fail(req, "507 Insufficient Storage", "The card is full.");
    }

    char dir_path[64];
    if (font || dictionary || picture) {
      std::snprintf(dir_path, sizeof(dir_path), "%s/%s", self->root,
                    font         ? "fonts"
                    : dictionary ? "dictionaries"
                                 : vellum::SleepPictures::kFolder);
    } else {
      self->books_dir(dir_path, sizeof(dir_path));
    }
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
    int silent = 0;
    while (left > 0) {
      const int n = httpd_req_recv(req, self->buffer, left < kChunk ? left : kChunk);
      if (n == HTTPD_SOCK_ERR_TIMEOUT && ++silent < kMaxSilentWaits) continue;
      silent = 0;
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

  // Deletes a sleep screen picture; the reader drops its converted copy on its next scan.
  static esp_err_t post_sleep_remove(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char body[512];
    char raw[384];
    char name[128];
    const int n = req->content_len < sizeof(body) ? httpd_req_recv(req, body, req->content_len) : -1;
    if (n <= 0 || !vellum::form_field(body, static_cast<size_t>(n), "name", raw, sizeof(raw)) ||
        !vellum::safe_picture_name(raw, name, sizeof(name)) || std::strcmp(raw, name) != 0) {
      return fail(req, "400 Bad Request", "Expected a picture's name.");
    }
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%s/%s", self->root, vellum::SleepPictures::kFolder, name);
    if (std::remove(path) != 0) return fail(req, "404 Not Found", "No such picture.");
    ESP_LOGI(kTag, "removed sleep picture %s", name);
    return httpd_resp_sendstr(req, "ok");
  }

  // Highlights for the phone page: listed as JSON, notes set, removed.
  static esp_err_t get_highlights(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    if (!self->load_highlights()) return fail(req, "500 Internal Server Error", "Out of memory.");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    self->highlights->write_json(
        [](void* ctx, const char* text, size_t len) {
          httpd_resp_send_chunk(static_cast<httpd_req_t*>(ctx), text, static_cast<ssize_t>(len));
        },
        req);
    return httpd_resp_send_chunk(req, nullptr, 0);
  }

  // The form body of a highlights request, and the highlight it names.
  static bool read_form(httpd_req_t* req, char* body, size_t size, size_t& len, uint32_t& id) {
    char number[16];
    const int n = req->content_len < size ? httpd_req_recv(req, body, req->content_len) : -1;
    if (n <= 0 || !vellum::form_field(body, static_cast<size_t>(n), "id", number, sizeof(number))) return false;
    len = static_cast<size_t>(n);
    id = static_cast<uint32_t>(std::strtoul(number, nullptr, 10));
    return id != 0;
  }

  static esp_err_t post_note(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    static char body[2048];  // one request at a time: the server has one task
    static char note[1024];
    size_t len = 0;
    uint32_t id = 0;
    if (!read_form(req, body, sizeof(body), len, id) || !vellum::form_field(body, len, "note", note, sizeof(note))) {
      return fail(req, "400 Bad Request", "Expected a highlight and a note.");
    }
    if (!self->load_highlights()) return fail(req, "500 Internal Server Error", "Out of memory.");
    if (!self->highlights->set_note(id, note)) return fail(req, "404 Not Found", "No such highlight.");
    if (!self->highlights->save(*self->card)) return fail(req, "500 Internal Server Error", "Could not save.");
    return httpd_resp_sendstr(req, "ok");
  }

  static esp_err_t post_remove(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char body[64];
    size_t len = 0;
    uint32_t id = 0;
    if (!read_form(req, body, sizeof(body), len, id)) return fail(req, "400 Bad Request", "Expected a highlight.");
    if (!self->load_highlights()) return fail(req, "500 Internal Server Error", "Out of memory.");
    const int k = self->highlights->find(id);
    if (k < 0) return fail(req, "404 Not Found", "No such highlight.");
    self->highlights->remove(k);
    if (!self->highlights->save(*self->card)) return fail(req, "500 Internal Server Error", "Could not save.");
    return httpd_resp_sendstr(req, "ok");
  }

  // The page's Settings section: everything it shows, as JSON.
  static esp_err_t get_settings(httpd_req_t* req) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    char ssid[33];
    char password[65];
    home_wifi_load(ssid, sizeof(ssid), password, sizeof(password));
    if (!self->load_card() || !vellum::phone_settings_json(*self->card, self->fonts, ssid, self->buffer, kChunk)) {
      return fail(req, "500 Internal Server Error", "Could not read the settings.");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, self->buffer);
  }

  // A form from the Settings section, applied to the card by `apply`;
  // `tell` has the app take it up at once.
  static esp_err_t post_form(httpd_req_t* req, vellum::PhoneSaved (*apply)(vellum::Storage&, const char*, size_t),
                             bool tell) {
    auto* self = static_cast<Impl*>(req->user_ctx);
    const int n = req->content_len < kChunk ? httpd_req_recv(req, self->buffer, req->content_len) : -1;
    if (n <= 0) return fail(req, "400 Bad Request", "Expected a form.");
    if (!self->load_card()) return fail(req, "500 Internal Server Error", "Out of memory.");
    switch (apply(*self->card, self->buffer, static_cast<size_t>(n))) {
      case vellum::PhoneSaved::Ok:
        break;
      case vellum::PhoneSaved::Bad:
        return fail(req, "400 Bad Request", "That value cannot be used.");
      case vellum::PhoneSaved::Full:
        return fail(req, "507 Insufficient Storage", "There is no room for more.");
      case vellum::PhoneSaved::NotSaved:
        return fail(req, "500 Internal Server Error", "Could not save.");
    }
    if (tell) self->update([](vellum::TransferStatus& s) { ++s.settings_saved; });
    return httpd_resp_sendstr(req, "ok");
  }

  static esp_err_t post_settings(httpd_req_t* req) { return post_form(req, vellum::phone_post_settings, true); }
  static esp_err_t post_sync(httpd_req_t* req) { return post_form(req, vellum::phone_post_sync, true); }
  // The store reads the catalogues when it opens: nothing to tell.
  static esp_err_t post_catalogues(httpd_req_t* req) { return post_form(req, vellum::phone_post_catalogues, false); }

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
    int silent = 0;
    while (got < req->content_len) {
      const size_t left = req->content_len - got;
      const int n = httpd_req_recv(req, self->buffer, left < kChunk ? left : kChunk);
      if (n == HTTPD_SOCK_ERR_TIMEOUT && ++silent < kMaxSilentWaits) continue;
      silent = 0;
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
    char body[256];
    vellum::PhoneTime t;
    const int n = req->content_len < sizeof(body) ? httpd_req_recv(req, body, req->content_len) : -1;
    if (n <= 0 || !vellum::parse_phone_time(body, static_cast<size_t>(n), t)) {
      return fail(req, "400 Bad Request", "Expected utc=YYYY-MM-DD HH:MM:SS.");
    }
    if (self->clock == nullptr || !self->clock->set(t.utc)) {
      return fail(req, "500 Internal Server Error", "The clock could not be set.");
    }
    ESP_LOGI(kTag, "clock set to %04u-%02u-%02u %02u:%02u UTC, zone %s %s", unsigned{t.utc.year}, unsigned{t.utc.month},
             unsigned{t.utc.day}, unsigned{t.utc.hour}, unsigned{t.utc.minute}, t.name, t.rule);
    // Redraw with the new time; the app takes up the zone.
    self->update([&](vellum::TransferStatus& s) {
      if (t.rule[0] == '\0') return;
      ++s.zones_sent;
      std::snprintf(s.zone_name, sizeof(s.zone_name), "%s", t.name);
      std::snprintf(s.zone_rule, sizeof(s.zone_rule), "%s", t.rule);
    });
    return httpd_resp_sendstr(req, "ok");
  }

  // The page on the home Wi-Fi: the reader joins it like the store does,
  // and the page's address is the one the router gave it.
  bool start_home(vellum::TransferInfo& info) {
    char ssid[33];
    char password[65];
    if (!home_wifi_load(ssid, sizeof(ssid), password, sizeof(password))) return false;
    if (home.join(info.cancelled, info.cancel_ctx) != vellum::UpdateResult::UpToDate) {
      home.leave();
      return false;
    }
    esp_netif_ip_info_t ip = {};
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta == nullptr || esp_netif_get_ip_info(sta, &ip) != ESP_OK || ip.ip.addr == 0) {
      home.leave();
      return false;
    }
    std::snprintf(info.network, sizeof(info.network), "%s", ssid);
    info.password[0] = '\0';
    std::snprintf(info.address, sizeof(info.address), "http://" IPSTR, IP2STR(&ip.ip));
    ESP_LOGI(kTag, "page at %s on %s", info.address, ssid);
    return true;
  }

  bool start_wifi(vellum::TransferInfo& info) {
    if (info.home) return start_home(info);
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
    cfg.ap.channel = EspNowPeers::kChannel;  // other readers send books on it too
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
    cfg.max_uri_handlers = 18;
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
        {.uri = "/highlights", .method = HTTP_GET, .handler = get_highlights, .user_ctx = this},
        {.uri = "/highlights/note", .method = HTTP_POST, .handler = post_note, .user_ctx = this},
        {.uri = "/highlights/remove", .method = HTTP_POST, .handler = post_remove, .user_ctx = this},
        {.uri = "/settings", .method = HTTP_GET, .handler = get_settings, .user_ctx = this},
        {.uri = "/settings", .method = HTTP_POST, .handler = post_settings, .user_ctx = this},
        {.uri = "/sync", .method = HTTP_POST, .handler = post_sync, .user_ctx = this},
        {.uri = "/catalogues", .method = HTTP_POST, .handler = post_catalogues, .user_ctx = this},
        {.uri = "/sleep", .method = HTTP_GET, .handler = get_sleep, .user_ctx = this},
        {.uri = "/sleep/remove", .method = HTTP_POST, .handler = post_sleep_remove, .user_ctx = this},
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
  // Full speed while it runs: the app waits for input meanwhile (at the
  // lowest speed) and the server's tasks do the work.
  if (m.awake == nullptr) esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "transfer", &m.awake);
  if (m.buffer == nullptr) return false;
  xSemaphoreTake(m.lock, portMAX_DELAY);
  m.st = {};
  xSemaphoreGive(m.lock);
  m.fonts = info.fonts;
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
  m.home.leave();  // the home Wi-Fi too, when the page was there
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

#include "esp_now_peers.h"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace {

constexpr const char* kTag = "peers";
constexpr int kQueueDepth = 6;  // packets waiting; the sender waits for each answer
constexpr uint8_t kBroadcast[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static_assert(vellum::PeerPacket::kMaxSize <= ESP_NOW_MAX_DATA_LEN_V2);

}  // namespace

struct EspNowPeers::Impl {
  NotifyFn notify = nullptr;
  QueueHandle_t queue = nullptr;
  esp_pm_lock_handle_t awake = nullptr;
  wifi_interface_t ifidx = WIFI_IF_STA;
  bool started = false;
  bool own_wifi = false;  // the radio was started here, not by the upload network
  char name[16] = {};

  static Impl* self;

  static void on_receive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    Impl* m = self;
    if (m == nullptr || m->queue == nullptr || len <= 0 || len > vellum::PeerPacket::kMaxSize) return;
    static vellum::PeerPacket packet;  // only the Wi-Fi task calls this
    std::memcpy(packet.from.bytes, info->src_addr, ESP_NOW_ETH_ALEN);
    std::memcpy(packet.data, data, static_cast<size_t>(len));
    packet.size = static_cast<uint16_t>(len);
    if (xQueueSend(m->queue, &packet, 0) == pdTRUE) m->notify();  // a full queue drops it; resends cover that
  }

  bool add_peer(const uint8_t* address) {
    if (esp_now_is_peer_exist(address)) return true;
    esp_now_peer_info_t peer = {};
    std::memcpy(peer.peer_addr, address, ESP_NOW_ETH_ALEN);
    peer.channel = 0;  // whatever the radio is on
    peer.ifidx = ifidx;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
  }

  bool start_radio() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    if (err != ESP_OK || esp_netif_init() != ESP_OK) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    if (esp_wifi_init(&init) != ESP_OK) return false;
    own_wifi = true;
    return esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK && esp_wifi_start() == ESP_OK &&
           esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
  }
};

EspNowPeers::Impl* EspNowPeers::Impl::self = nullptr;

EspNowPeers::EspNowPeers(NotifyFn notify) : impl_(new Impl) {
  impl_->notify = notify;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);  // the same name as the reader's upload network
  std::snprintf(impl_->name, sizeof(impl_->name), "Vellum-%02X%02X", mac[4], mac[5]);
}

const char* EspNowPeers::name() { return impl_->name; }

bool EspNowPeers::start() {
  Impl& m = *impl_;
  stop();
  if (m.queue == nullptr) m.queue = xQueueCreate(kQueueDepth, sizeof(vellum::PeerPacket));
  if (m.awake == nullptr) esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "peers", &m.awake);
  if (m.queue == nullptr) return false;

  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_NULL) {
    m.ifidx = mode == WIFI_MODE_AP ? WIFI_IF_AP : WIFI_IF_STA;  // the upload network is up
  } else {
    m.ifidx = WIFI_IF_STA;
    if (!m.start_radio()) {
      ESP_LOGW(kTag, "radio did not start");
      stop();
      return false;
    }
  }
  esp_wifi_set_ps(WIFI_PS_NONE);  // answer at once rather than at the next beacon
  if (m.awake != nullptr) esp_pm_lock_acquire(m.awake);
  Impl::self = &m;
  m.started = true;  // from here stop() undoes everything
  if (esp_now_init() != ESP_OK || esp_now_register_recv_cb(Impl::on_receive) != ESP_OK || !m.add_peer(kBroadcast)) {
    ESP_LOGW(kTag, "ESP-NOW did not start");
    stop();
    return false;
  }
  ESP_LOGI(kTag, "%s listening on channel %u", m.name, kChannel);
  return true;
}

void EspNowPeers::stop() {
  Impl& m = *impl_;
  if (m.started) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    if (m.awake != nullptr) esp_pm_lock_release(m.awake);
    m.started = false;
  }
  if (m.own_wifi) {
    esp_wifi_stop();
    esp_wifi_deinit();
    m.own_wifi = false;
  }
  Impl::self = nullptr;
  if (m.queue != nullptr) xQueueReset(m.queue);
}

bool EspNowPeers::send(const vellum::PeerAddress* to, const void* data, uint32_t len) {
  Impl& m = *impl_;
  if (!m.started || len > vellum::PeerPacket::kMaxSize) return false;
  const uint8_t* address = to != nullptr ? to->bytes : kBroadcast;
  if (!m.add_peer(address)) return false;
  return esp_now_send(address, static_cast<const uint8_t*>(data), len) == ESP_OK;
}

bool EspNowPeers::receive(vellum::PeerPacket& out) {
  return impl_->queue != nullptr && xQueueReceive(impl_->queue, &out, 0) == pdTRUE;
}

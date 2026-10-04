// Packets straight to other readers nearby over ESP-NOW, with no network in
// between. Shares the radio with WifiTransfer: on the receiving reader the
// upload network is already up and ESP-NOW rides on it; on the sending
// reader the radio is started here, tuned to the same channel.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "vellum/hal.h"

class EspNowPeers : public vellum::Peers {
 public:
  using NotifyFn = void (*)();

  // `notify` is called from the Wi-Fi task when a packet arrives.
  explicit EspNowPeers(NotifyFn notify);

  bool start() override;
  void stop() override;
  const char* name() override;
  bool send(const vellum::PeerAddress* to, const void* data, uint32_t len) override;
  bool receive(vellum::PeerPacket& out) override;

  // The channel both the upload network and ESP-NOW use.
  static constexpr uint8_t kChannel = 6;

 private:
  struct Impl;
  Impl* impl_;
};

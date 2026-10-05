// The free book store's way to the internet: HTTPS GETs over the home
// Wi-Fi with esp_http_client and the certificate bundle, the body handed
// on piece by piece as it arrives.
#pragma once

#include "vellum/hal.h"
#include "wifi_common.h"

class HttpNetwork : public vellum::Network {
 public:
  vellum::NetResult get(const char* url, BodyFn body, void* ctx) override;
  void disconnect() override { wifi_.leave(); }

 private:
  HomeWifi wifi_;  // joined by the first get(), until disconnect()
};

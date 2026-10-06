// The store's and KOReader sync's way to the internet: HTTPS requests over
// the home Wi-Fi with esp_http_client and the certificate bundle, the body
// handed on piece by piece as it arrives.
#pragma once

#include "vellum/hal.h"
#include "wifi_common.h"

class HttpNetwork : public vellum::Network {
 public:
  vellum::NetResult get(const char* url, BodyFn body, void* ctx) override;
  vellum::NetResult request(vellum::HttpRequest& req, BodyFn body, void* ctx) override;
  void disconnect() override { wifi_.leave(); }

 private:
  // Joins the home Wi-Fi unless joined already; Ok, NoNetwork or NoWifi.
  vellum::NetResult join();

  HomeWifi wifi_;  // joined by the first get(), until disconnect()
};

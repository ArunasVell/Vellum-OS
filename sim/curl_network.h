// The store on the desktop simulator, with the computer's internet through
// libcurl (built in when CMake finds it).
#pragma once

#include <curl/curl.h>

#include "vellum/hal.h"

class CurlNetwork : public vellum::Network {
 public:
  CurlNetwork() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlNetwork() override { curl_global_cleanup(); }

  vellum::NetResult get(const char* url, BodyFn body, void* ctx) override {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return vellum::NetResult::NoServer;
    Call call{curl, body, ctx};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Vellum");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);  // 4xx and 5xx are errors, not bodies
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlNetwork::write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &call);
    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (call.stopped) return vellum::NetResult::Stopped;
    if (rc == CURLE_OK) return vellum::NetResult::Ok;
    return call.started ? vellum::NetResult::BrokeOff : vellum::NetResult::NoServer;
  }

  void disconnect() override {}

 private:
  struct Call {
    CURL* curl;
    BodyFn body;
    void* ctx;
    bool started = false;
    bool stopped = false;
  };

  static size_t write(char* data, size_t size, size_t count, void* user) {
    auto& call = *static_cast<Call*>(user);
    call.started = true;
    curl_off_t total = 0;
    curl_easy_getinfo(call.curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &total);
    const size_t len = size * count;
    if (!call.body(call.ctx, reinterpret_cast<const uint8_t*>(data), static_cast<uint32_t>(len),
                   total > 0 ? static_cast<uint32_t>(total) : 0)) {
      call.stopped = true;
      return 0;  // stops the transfer
    }
    return len;
  }
};

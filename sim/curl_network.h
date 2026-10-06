// The store and KOReader sync on the desktop simulator, with the computer's
// internet through libcurl (built in when CMake finds it).
#pragma once

#include <curl/curl.h>

#include <cstring>
#include <string>

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

  vellum::NetResult request(vellum::HttpRequest& req, BodyFn body, void* ctx) override {
    req.status = 0;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return vellum::NetResult::NoServer;
    Call call{curl, body, ctx};
    curl_slist* headers = nullptr;
    // "Name: value\r\n" lines, one list item each.
    for (const char* line = req.headers; line != nullptr && *line != '\0';) {
      const char* end = std::strstr(line, "\r\n");
      const std::string header(line, end != nullptr ? static_cast<size_t>(end - line) : std::strlen(line));
      headers = curl_slist_append(headers, header.c_str());
      line = end != nullptr ? end + 2 : line + header.size();
    }
    const bool get = std::strcmp(req.method, "GET") == 0;
    curl_easy_setopt(curl, CURLOPT_URL, req.url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, get ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Vellum");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method);
    if (headers != nullptr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (req.body != nullptr) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body_len));
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlNetwork::write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &call);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (call.stopped) return vellum::NetResult::Stopped;
    if (rc != CURLE_OK) return call.started ? vellum::NetResult::BrokeOff : vellum::NetResult::NoServer;
    req.status = static_cast<int>(status);
    return vellum::NetResult::Ok;
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

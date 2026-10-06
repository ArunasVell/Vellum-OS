// The store in the simulators without internet: a canned catalogue made of
// the test feeds (tests/data/store), marked as a demo on screen. Every
// download is the same sample book. KOReader sync answers as a server
// that keeps one position (in memory) would.
#pragma once

#include <cstdio>
#include <string>
#include <utility>

#include "vellum/hal.h"

class DemoNetwork : public vellum::Network {
 public:
  // `dir` holds store/*.opds, store/*.atom and sample.epub.
  explicit DemoNetwork(std::string dir) : dir_(std::move(dir)) {}

  vellum::NetResult get(const char* url, BodyFn body, void* ctx) override {
    const std::string u = url;
    std::string data;
    if (u.find("standardebooks.org/feeds") != std::string::npos) {
      data = read("store/standard-ebooks-new.atom");
    } else if (u.find("start_index=") != std::string::npos) {
      data = read("store/gutenberg-popular-2.opds");
    } else if (u.find("l.lt") != std::string::npos) {
      data = read("store/gutenberg-lt.opds");
    } else if (u.find("search.opds") != std::string::npos) {
      data = read("store/gutenberg-popular.opds");
    } else if (u.find(".opds") != std::string::npos) {
      data = book_feed(u);
    } else if (u.find("epub") != std::string::npos) {
      data = read("sample.epub");
    }
    if (data.empty()) return vellum::NetResult::NoServer;
    for (size_t i = 0; i < data.size(); i += 4096) {
      const size_t n = data.size() - i < 4096 ? data.size() - i : 4096;
      if (!body(ctx, reinterpret_cast<const uint8_t*>(data.data() + i), static_cast<uint32_t>(n),
                static_cast<uint32_t>(data.size())))
        return vellum::NetResult::Stopped;
    }
    return vellum::NetResult::Ok;
  }

  vellum::NetResult request(vellum::HttpRequest& req, BodyFn body, void* ctx) override {
    const std::string u = req.url;
    std::string answer;
    req.status = 200;
    if (u.find("/syncs/progress") != std::string::npos) {
      if (std::string(req.method) == "PUT") {
        progress_ = req.body != nullptr ? std::string(req.body, req.body_len) : "";
        answer = "{\"document\":\"demo\",\"timestamp\":1760000000}";
      } else {
        answer = progress_.empty() ? "{}" : progress_;
      }
    } else if (u.find("/users/create") != std::string::npos) {
      req.status = 201;
      answer = "{\"username\":\"demo\"}";
    } else if (std::string(req.method) == "GET") {
      const vellum::NetResult r = get(req.url, body, ctx);
      req.status = r == vellum::NetResult::Ok ? 200 : 404;
      return r == vellum::NetResult::NoServer ? vellum::NetResult::Ok : r;
    } else {
      req.status = 404;
    }
    if (!answer.empty() && !body(ctx, reinterpret_cast<const uint8_t*>(answer.data()),
                                 static_cast<uint32_t>(answer.size()), static_cast<uint32_t>(answer.size()))) {
      return vellum::NetResult::Stopped;
    }
    return vellum::NetResult::Ok;
  }

  void disconnect() override {}
  bool demo() const override { return true; }

 private:
  std::string read(const char* name) const {
    std::string out;
    if (std::FILE* f = std::fopen((dir_ + "/" + name).c_str(), "rb")) {
      char buf[4096];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
      std::fclose(f);
    }
    return out;
  }

  // One book's own feed: the sample one for Pride and Prejudice, else made
  // from the book's entry in the canned lists.
  std::string book_feed(const std::string& url) const {
    if (url.find("/1342.opds") != std::string::npos) return read("store/gutenberg-book.opds");
    const size_t slash = url.rfind('/');
    const std::string id = "<id>https://www.gutenberg.org/ebooks" + url.substr(slash);
    for (const char* list :
         {"store/gutenberg-popular.opds", "store/gutenberg-popular-2.opds", "store/gutenberg-lt.opds"}) {
      const std::string feed = read(list);
      const size_t at = feed.find(id + "</id>");
      if (at == std::string::npos) continue;
      const size_t start = feed.rfind("<entry>", at);
      const size_t end = feed.find("</id>", at);
      std::string entry = feed.substr(start, end - start);  // title and author, up to the id
      const size_t content = entry.find("<content type=\"text\">");
      if (content != std::string::npos) {
        entry.replace(content, 21, "<author><name>");
        entry.replace(entry.find("</content>"), 10, "</name></author>");
      }
      return "<feed xmlns=\"http://www.w3.org/2005/Atom\">" + entry +
             "</id><summary>A demo entry: the simulator has no internet, so every download is the same "
             "sample book.</summary><link type=\"application/epub+zip\" rel=\"http://opds-spec.org/acquisition\" "
             "length=\"" +
             std::to_string(read("sample.epub").size()) + "\" href=\"" + url.substr(0, url.size() - 5) +
             ".epub3.images\"/></entry></feed>";
    }
    return {};
  }

  std::string dir_;
  std::string progress_;  // the last position sent to the sync server
};

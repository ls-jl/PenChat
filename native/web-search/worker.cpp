// PenChat tool worker. Apache-2.0; Edge Search remains MIT.
#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <edgesearch/browser.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "trust.hpp"
using Json = nlohmann::json;
namespace {
volatile sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
void transfer(int fd, void *buffer, size_t length, bool write) {
  auto p = static_cast<char *>(buffer);
  while (length) {
    const auto n = write ? send(fd, p, length, MSG_NOSIGNAL) : recv(fd, p, length, 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      throw std::runtime_error("IPC disconnected");
    p += n;
    length -= size_t(n);
  }
}
std::string prefix(std::string text, size_t limit) {
  if (text.size() <= limit)
    return text;
  while (limit && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80)
    --limit;
  text.resize(limit);
  return text;
}
class Worker {
  edgesearch::Browser browser_;
  std::set<std::string> known_;
  void remember(const std::string &url) {
    if (known_.size() < 4096)
      known_.insert(url);
  }
  Json page(edgesearch::Page p, bool completeText = false, size_t linkOffset = 0,
            bool onlyLinks = false) {
    remember(p.url);
    auto j = Json::parse(edgesearch::to_json(p));
    j["text"] = onlyLinks ? std::string() : prefix(p.text, completeText ? 2 * 1024 * 1024 : 8192);
    j["next_offset_bytes"] = j["text"].get<std::string>().size();
    j["more"] = p.text.size() > j["next_offset_bytes"].get<size_t>();
    // Image data is only returned by the explicit image operation.
    j.erase("image");
    j.erase("image_data");
    Json links = Json::array();
    size_t bytes = 0, index = 0, next = linkOffset;
    for (const auto &link : p.links) {
      remember(link.url);
      if (index++ < linkOffset)
        continue;
      const auto weight = link.url.size() + std::min(link.text.size(), size_t(256));
      if (links.size() >= 16 || (bytes + weight > 4096 && !links.empty()))
        break;
      bytes += weight;
      next = index;
      links.push_back({{"id", link.id},
                       {"url", link.url},
                       {"text", prefix(link.text, 256)},
                       {"kind", link.kind}});
    }
    j["next_link_offset"] = next;
    j["more_links"] = next < p.links.size();
    j["links_truncated"] = p.links_truncated || next < p.links.size();
    j["links"] = std::move(links);
    return j;
  }

public:
  explicit Worker(edgesearch::BrowserOptions options) : browser_(std::move(options)) {}
  Json run(const Json &request) {
    auto name = request.at("name").get<std::string>();
    auto a = request.at("arguments");
    if (!a.is_object())
      throw std::runtime_error("Tool arguments must be an object");
    if (name == "seed_sources") {
      const auto urls = a.at("urls");
      if (!urls.is_array() || urls.size() > 64)
        throw std::runtime_error("Invalid source index");
      for (const auto &url : urls) {
        const auto value = url.get<std::string>();
        if (value.size() <= 4096 && (value.find("https://") == 0 || value.find("http://") == 0))
          remember(value);
      }
      return {{"seeded", true}};
    }
    if (name == "web_search") {
      auto query = a.at("query").get<std::string>();
      if (query.empty() || query.size() > 2048)
        throw std::runtime_error("Invalid query length");
      auto response = browser_.search(query);
      auto j = Json::parse(edgesearch::to_json(response));
      for (const auto &r : response.results)
        remember(r.url);
      return j;
    }
    if (name == "web_open" || name == "read_source") {
      auto url = a.at("url").get<std::string>();
      if (name != "read_source" && !known_.count(url))
        throw std::runtime_error("Open only URLs returned by search or page links");
      return page(browser_.open(url), name == "read_source");
    }
    auto id = a.value("page_id", uint64_t(0));
    if (name == "web_click")
      return page(browser_.click(id, a.at("link_id").get<size_t>()));
    if (name == "web_read") {
      if (a.contains("link_offset"))
        return page(browser_.page(id), false, a.at("link_offset").get<size_t>(), true);
      auto text = browser_.read_bytes(id, a.value("offset", size_t(0)), 16384);
      return Json::parse(edgesearch::to_json(text));
    }
    if (name == "web_find") {
      auto query = a.at("text").get<std::string>();
      if (query.empty() || query.size() > 1024)
        throw std::runtime_error("Invalid find text");
      Json matches = Json::array();
      for (const auto &m : browser_.find(id, query, 20))
        matches.push_back(
            {{"line", m.line}, {"text", prefix(m.text, 512)}, {"offset", m.offset_bytes}});
      return {{"matches", matches}};
    }
    if (name == "web_read_image") {
      const auto source = browser_.page(id);
      auto linkId = a.at("link_id").get<size_t>();
      bool valid = false;
      for (const auto &link : source.links)
        if (link.id == linkId && link.kind == "image")
          valid = true;
      if (!valid)
        throw std::runtime_error("Image must be a returned image link");
      const auto p = browser_.click(id, linkId);
      if (p.resource_type != "image" || p.image_data.empty() ||
          p.image_data.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Image unavailable or exceeds 2MiB");
      auto j = Json::parse(edgesearch::to_json(p));
      j["source_url"] = source.url;
      j["source_title"] = source.title;
      return j;
    }
    throw std::runtime_error("Unknown tool");
  }
};
} // namespace
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const int fd = std::stoi(argv[1]);
  if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0 || getppid() == 1)
    return 2;
  signal(SIGPIPE, SIG_IGN);
  signal(SIGUSR1, interrupt);
  try {
    edgesearch::BrowserOptions options;
    options.cache_bytes = 6 * 1024 * 1024;
    options.max_pages = 8;
    options.network.max_body_bytes = 2 * 1024 * 1024;
    options.network.timeout_ms = 15000;
    options.network.cancelled = [] { return interrupted != 0; };
    std::string trustError;
    try {
      options.network.ca_bundle = penchat::bundledWebTrust();
    } catch (const std::exception &e) {
      trustError = e.what();
    }
    Worker worker(options);
    bool reader = false;
    for (;;) {
      uint32_t length = 0;
      transfer(fd, &length, 4, false);
      length = ntohl(length);
      if (!length || length > 16384)
        return 3;
      std::string text(length, '\0');
      transfer(fd, &text[0], text.size(), false);
      Json result, request = Json::parse(text);
      interrupted = 0;
      try {
        if (request.value("name", std::string()) == "hello")
          result = {{"component", "0.14.0"}, {"protocol", 1}};
        else {
          if (!trustError.empty())
            throw std::runtime_error(trustError);
          if (request.value("name", std::string()) == "read_source" && !reader) {
            options.cache_bytes = 2 * 1024 * 1024;
            options.max_pages = 4;
            // Leave room for links, indexes and page metadata in the reader's
            // independent 2MiB retained cache (upstream defaults need >4MiB).
            options.max_text_bytes = 1024 * 1024;
            options.max_link_bytes = 256 * 1024;
            worker = Worker(options);
            reader = true;
          }
          result = worker.run(request);
        }
        result = {{"ok", true}, {"result", std::move(result)}};
      } catch (const std::exception &e) {
        result = {{"ok", false}, {"error", prefix(e.what(), 1024)}};
      }
      result["request"] = request.at("request");
      auto data = result.dump();
      if (data.size() > 4 * 1024 * 1024)
        data = Json({{"ok", false},
                     {"error", "Tool response too large"},
                     {"request", request.at("request")}})
                   .dump();
      length = htonl(uint32_t(data.size()));
      transfer(fd, &length, 4, true);
      transfer(fd, &data[0], data.size(), true);
    }
  } catch (...) {
    close(fd);
    return 1;
  }
}

// Test-only HTTP/SSE fixture; binds loopback and never logs request bodies or
// keys.
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <fstream>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
using Json = nlohmann::json;
static std::mutex logMutex;
static volatile sig_atomic_t running = 1;
static int listener = -1;
static void stop(int) {
  running = 0;
  if (listener >= 0)
    close(listener);
}
static std::string read(const char *path) {
  std::ifstream f(path);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
static bool sendAll(int fd, const std::string &text) {
  size_t n = 0;
  while (n < text.size()) {
    auto sent = send(fd, text.data() + n, text.size() - n, MSG_NOSIGNAL);
    if (sent <= 0)
      return false;
    n += sent;
  }
  return true;
}
static void request(int fd) {
  std::string incoming;
  char buffer[8192];
  size_t headerEnd;
  while ((headerEnd = incoming.find("\r\n\r\n")) == std::string::npos) {
    auto n = recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      close(fd);
      return;
    }
    incoming.append(buffer, n);
    if (incoming.size() > 65536) {
      close(fd);
      return;
    }
  }
  auto headers = incoming.substr(0, headerEnd);
  auto lengthAt = headers.find("Content-Length:");
  if (lengthAt == std::string::npos)
    lengthAt = headers.find("content-length:");
  size_t size =
      lengthAt == std::string::npos ? 0 : std::stoul(headers.substr(lengthAt + 15));
  if (size > 32 * 1024 * 1024) {
    close(fd);
    return;
  }
  std::string body = incoming.substr(headerEnd + 4);
  while (body.size() < size) {
    auto n = recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      close(fd);
      return;
    }
    body.append(buffer, n);
  }
  auto path = headers.substr(headers.find(' ') + 1);
  path = path.substr(0, path.find(' '));
  auto mode = read("/tmp/penchat-http-mode");
  while (!mode.empty() && isspace(static_cast<unsigned char>(mode.back())))
    mode.pop_back();
  if (mode.empty())
    mode = "ok";
  auto parsed = Json::parse(body, nullptr, false);
  if (!parsed.is_object())
    parsed = Json::object();
  if (body.find("[penchat-title]") != std::string::npos) {
    {
      std::lock_guard<std::mutex> lock(logMutex);
      std::ofstream log("/tmp/penchat-title-requests.jsonl", std::ios::app);
      log << Json({{"model", parsed.value("model", std::string())}}).dump() << '\n';
    }
    bool chat = path.find("/chat/completions") != std::string::npos;
    sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: "
                "text/event-stream\r\nConnection: close\r\n\r\n");
    Json payload =
        chat
            ? Json{{"choices",
                    Json::array({{{"delta", {{"content", "图片与公式验证"}}}}})}}
            : Json{{"type", "response.output_text.delta"}, {"delta", "图片与公式验证"}};
    sendAll(fd, "data: " + payload.dump() + "\n\ndata: " +
                    (chat ? std::string("[DONE]")
                          : std::string("{\"type\":\"response.completed\"}")) +
                    "\n\n");
    close(fd);
    return;
  }
  const bool compression =
      body.find("PenChat_CONTEXT_COMPRESSION_V1") != std::string::npos;
  int systemCount = parsed.value("instructions", std::string()).empty() ? 0 : 1;
  int images = 0;
  for (const auto &item :
       parsed.value("messages", parsed.value("input", Json::array()))) {
    if (item.value("role", std::string()) == "system")
      ++systemCount;
    auto content = item.value("content", Json());
    if (content.is_array())
      for (const auto &part : content) {
        const auto type = part.value("type", std::string());
        if (type == "input_image" || type == "image_url")
          ++images;
      }
  }
  auto reasoning = parsed.value("reasoning", Json::object());
  size_t replayCount = 0;
  if (parsed.contains("input") && parsed["input"].is_array())
    for (const auto &item : parsed["input"])
      if (item.value("type", std::string()) == "reasoning")
        ++replayCount;
  {
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream log("/tmp/penchat-http-requests.jsonl", std::ios::app);
    log << Json({{"path", path},
                 {"mode", mode},
                 {"model", parsed.value("model", std::string())},
                 {"effort", parsed.value("reasoning_effort",
                                         reasoning.value("effort", std::string()))},
                 {"reasoning_items", replayCount},
                 {"compression", compression},
                 {"images", images},
                 {"system_count", systemCount},
                 {"has_reference",
                  body.find("此前对话的历史参考摘要") != std::string::npos},
                 {"has_old0", body.find("fact-0:") != std::string::npos},
                 {"has_pending", body.find("DEVICE_PENDING") != std::string::npos},
                 {"has_image", body.find("data:image/") != std::string::npos}})
               .dump()
        << '\n';
  }
  if (mode == "error" || mode == "unicode") {
    std::string error = "{\"error\":{\"message\":\"fixture unauthorized\"}}";
    if (mode == "unicode") {
      error.clear();
      for (int i = 0; i < 300; ++i)
        error += "错误说明";
    }
    sendAll(fd, "HTTP/1.1 401 Unauthorized\r\nContent-Type: "
                "application/json\r\nContent-Length: " +
                    std::to_string(error.size()) + "\r\nConnection: close\r\n\r\n" +
                    error);
    close(fd);
    return;
  }
  sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: "
              "text/event-stream\r\nConnection: close\r\n\r\n");
  if (compression) {
    usleep(mode == "context-delay" ? 2500000 : 350000);
    const bool chat = path.find("/chat/completions") != std::string::npos;
    Json event =
        chat ? Json{{"choices",
                     Json::array(
                         {{{"delta",
                            {{"content",
                              "设备摘要：保留事实、约束、决定和图片信息。"}}}}})}}
             : Json{{"type", "response.output_text.delta"},
                    {"delta", "设备摘要：保留事实、约束、决定和图片信息。"}};
    auto wire = "data: " + event.dump() + "\n\n";
    for (size_t i = 0; i < wire.size(); i += 7)
      if (!sendAll(fd, wire.substr(i, 7))) {
        close(fd);
        return;
      }
    sendAll(fd, "data: " +
                    (chat ? std::string("[DONE]")
                          : std::string("{\"type\":\"response.completed\"}")) +
                    "\n\n");
    close(fd);
    return;
  }
  auto reply = read("/tmp/penchat-http-reply.md");
  bool chat = path.find("/chat/completions") != std::string::npos;
  const bool thinking = mode == "reasoning" || mode == "encrypted";
  Json reasonItem = {{"id", "rs-fixture"},
                     {"type", "reasoning"},
                     {"status", "completed"},
                     {"encrypted_content", "device-test-opaque"},
                     {"summary", Json::array()}};
  if (thinking) {
    reply = "推理链路验证完成。";
    auto emit = [&](const Json &event) {
      return sendAll(fd, "data: " + event.dump() + "\n\n");
    };
    emit({{"type", "response.output_item.added"},
          {"output_index", 0},
          {"item", {{"type", "reasoning"}, {"id", "rs-fixture"}}}});
    if (mode == "reasoning") {
      emit({{"type", "response.reasoning_summary_text.delta"},
            {"output_index", 0},
            {"delta", "这是公开的思考总结。"}});
      reasonItem["summary"] =
          Json::array({{{"type", "summary_text"}, {"text", "这是公开的思考总结。"}}});
    }
    usleep(1100000);
    emit({{"type", "response.output_item.done"},
          {"output_index", 0},
          {"item", reasonItem}});
  }
  std::vector<std::string> parts;
  if (mode == "cancel") {
    for (size_t i = 0; i < reply.size();) {
      unsigned char c = reply[i];
      size_t n = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
      n = std::min(n, reply.size() - i);
      parts.push_back(reply.substr(i, n));
      i += n;
    }
  } else {
    size_t split = 0;
    for (int i = 0; i < 3; ++i) {
      size_t end = i == 2 ? reply.size() : reply.find('\n', split + reply.size() / 4);
      if (end == std::string::npos)
        end = reply.size();
      parts.push_back(reply.substr(split, end - split));
      split = end;
    }
  }
  for (auto &part : parts) {
    Json payload =
        chat ? Json{{"choices", Json::array({{{"delta", {{"content", part}}}}})}}
             : Json{{"type", "response.output_text.delta"},
                    {"output_index", thinking ? 1 : 0},
                    {"delta", part}};
    auto event = "data: " + payload.dump() + "\r\n\r\n";
    for (size_t i = 0; i < event.size(); i += 7)
      if (!sendAll(fd, event.substr(i, 7))) {
        close(fd);
        return;
      }
    usleep(mode == "cancel" ? 150000 : 60000);
  }
  if (mode != "cut") {
    Json completed = {{"type", "response.completed"}};
    if (thinking)
      completed["response"] = {
          {"output", Json::array({reasonItem,
                                  {{"type", "message"},
                                   {"content", Json::array({{{"type", "output_text"},
                                                             {"text", reply}}})}}})}};
    sendAll(fd, "data: " + (chat ? std::string("[DONE]") : completed.dump()) + "\n\n");
  }
  close(fd);
}
int main() {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, stop);
  listener = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(18773);
  if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
      listen(listener, 8))
    return 1;
  while (running) {
    int fd = accept(listener, nullptr, nullptr);
    if (fd < 0)
      break;
    std::thread([fd] {
      try {
        request(fd);
      } catch (...) {
        close(fd);
      }
    }).detach();
  }
  return 0;
}

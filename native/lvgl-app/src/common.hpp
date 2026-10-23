#pragma once
#include "app_identity.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace penchat {
using Json = nlohmann::json;
inline int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
inline int64_t ticks() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
std::string uid(const std::string &prefix);
std::string join(const std::string &root, const std::string &leaf);
void ensureDirectory(const std::string &path);
std::string fileText(const std::string &path, size_t limit = 32 * 1024 * 1024);
void atomicText(const std::string &path, const std::string &content);
std::string utf8(uint32_t cp);
std::vector<std::string> characters(const std::string &value);
struct Event {
  std::string type;
  Json data;
};
class Events {
  std::mutex mutex_;
  std::deque<Event> queue_;
  bool latestStateOnly_ = false;

public:
  explicit Events(bool latestStateOnly = false)
      : latestStateOnly_(latestStateOnly) {}
  void push(std::string type, Json data = Json::object()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestStateOnly_ && type == "response_state")
      queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                  [](const Event &event) {
                                    return event.type == "response_state";
                                  }),
                   queue_.end());
    queue_.push_back({std::move(type), std::move(data)});
  }
  // Preview/progress producers keep one pending update per scan. Terminal
  // events use push(), preserving every stop, finish and error notification.
  void pushLatest(std::string type, Json data) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &event : queue_)
      if (event.type == type &&
          event.data.value("generation", uint64_t(0)) ==
              data.value("generation", uint64_t(0)) &&
          event.data.value("scanId", uint64_t(0)) ==
              data.value("scanId", uint64_t(0))) {
        event.data = std::move(data);
        return;
      }
    queue_.push_back({std::move(type), std::move(data)});
  }
  std::deque<Event> drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<Event> result;
    result.swap(queue_);
    return result;
  }
};
struct Screen {
  int width = 960, height = 266, rotation = 270, xoffset = 0, yoffset = 107;
  static Screen fromConfig(const Json &cfg);
};
} // namespace penchat

#pragma once
#include "capture.h"
#include "common.hpp"
#include "markdown.hpp"
#include <set>
namespace penchat {
struct ImageFrame {
  int width = 0, height = 0;
  std::vector<uint8_t> pixels;
};
class Images {
  struct Request {
    std::string path, key;
    int width, height;
  };
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Request> queue_;
  std::map<std::string, std::shared_ptr<ImageFrame>> cache_;
  std::deque<std::string> order_;
  std::set<std::string> pending_;
  size_t bytes_ = 0;
  bool stopped_ = false;
  std::atomic<bool> cancel_{false};
  std::thread worker_;
  Events &events_;
  void work();

public:
  explicit Images(Events &events);
  ~Images();
  std::string request(const std::string &path, int width, int height);
  std::shared_ptr<ImageFrame> get(const std::string &key);
  bool failed(const std::string &key);
};
class Camera {
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::mutex mutex_;
  std::vector<uint8_t> nv12_;
  int width_ = 0, height_ = 0;
  std::shared_ptr<ImageFrame> frame_;
  Events &events_;
  std::atomic<bool> save_{false};
  std::string directory_;
  void work();

public:
  Camera(Events &events, std::string directory)
      : events_(events), directory_(std::move(directory)) {}
  ~Camera() { stop(); }
  void start();
  void stop();
  void capture() { save_ = true; }
  std::shared_ptr<ImageFrame> frame();
};
} // namespace penchat

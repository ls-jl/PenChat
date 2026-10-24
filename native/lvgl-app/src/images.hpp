#pragma once
#include "capture.h"
#include "common.hpp"
#include "markdown.hpp"
#include <set>
namespace penchat {
struct ImageFrame {
  int width = 0, height = 0, rotation = 0;
  std::vector<uint8_t> pixels;
};
class Images {
  struct Request {
    std::string path, key;
    int width, height;
    bool upscale, detail;
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
  std::string request(const std::string &path, int width, int height,
                      bool upscale = false, bool detail = false);
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
  std::string directory_, workspace_;
  std::atomic<int> orientation_{0}, manualOrientation_{-1}, captureOrientation_{0};
  void work();

public:
  Camera(Events &events, std::string directory, std::string workspace)
      : events_(events), directory_(std::move(directory)), workspace_(std::move(workspace)) {}
  ~Camera() { stop(); }
  void start();
  void stop();
  void capture(int angle = 0) { captureOrientation_ = angle; save_ = true; }
  void orientation(bool automatic, int angle) { orientation_ = angle; manualOrientation_ = automatic ? -1 : angle; }
  std::shared_ptr<ImageFrame> frame();
};
} // namespace penchat

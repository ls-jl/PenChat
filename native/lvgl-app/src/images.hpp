#pragma once
#include "capture.h"
#include "common.hpp"
#include "markdown.hpp"
#include "photo_orientation.hpp"
#include <set>
namespace penchat {
struct ImageFrame {
  int width = 0, height = 0, rotation = 0;
  int cameraPose = 0; // Pose tag; rotation describes the actual preview pixels.
  bool opaque = false;
  std::vector<uint8_t> pixels;
};
class Images {
  struct Request {
    std::string path, key;
    int width, height;
    bool upscale;
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
  void trimCache(size_t incoming);

public:
  explicit Images(Events &events);
  ~Images();
  void stop();
  std::string request(const std::string &path, int width, int height,
                      bool upscale = false);
  std::shared_ptr<ImageFrame> get(const std::string &key);
  bool failed(const std::string &key);
};
class Camera {
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> generation_{0};
  std::thread thread_;
  std::mutex mutex_;
  std::shared_ptr<ImageFrame> frame_;
  Events &events_;
  std::atomic<bool> save_{false};
  std::string directory_, workspace_;
  const bool fixedWorkspace_;
  std::atomic<int> previewWidth_{0}, previewHeight_{0};
  std::atomic<int> orientation_{0}, manualOrientation_{-1}, captureOrientation_{0};
  void work();

public:
  Camera(Events &events, std::string directory, std::string workspace, bool fixedWorkspace)
      : events_(events), directory_(std::move(directory)), workspace_(std::move(workspace)),
        fixedWorkspace_(fixedWorkspace) {}
  ~Camera() { stop(); }
  void start();
  void stop();
  uint64_t generation() const { return generation_.load(); }
  PhotoTransforms transforms(int pose) const;
  void previewSize(int width,int height) {previewWidth_=width;previewHeight_=height;}
  void capture(int angle = 0) { captureOrientation_ = photoTransforms(angle, fixedWorkspace_).saved; save_ = true; }
  void orientation(bool automatic, int angle) { orientation_ = angle; manualOrientation_ = automatic ? -1 : angle; }
  std::shared_ptr<ImageFrame> frame();
};
} // namespace penchat

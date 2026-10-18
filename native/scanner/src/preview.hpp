#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace penchat_scan {
struct PreviewFrame {
  uint64_t generation = 0, scan = 0;
  unsigned width = 0, height = 0;
  int canvasWidth = 0, canvasHeight = 0;
  std::vector<uint8_t> pixels;
};

// Canvas reads can wait behind drawing. They must never run on the capture loop.
// There is at most one reader, one forced final request, and one latest frame.
class PreviewWorker {
  struct Task {
    uint64_t epoch;
    std::function<std::unique_ptr<PreviewFrame>()> read;
  };
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopped_ = false, busy_ = false;
  uint64_t epoch_ = 0;
  std::unique_ptr<Task> pending_;
  std::unique_ptr<PreviewFrame> latest_;
  std::thread thread_;
  void run() {
    for (;;) {
      std::unique_ptr<Task> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopped_ || pending_; });
        if (stopped_)
          return;
        task = std::move(pending_);
        busy_ = true;
      }
      const auto epoch = task->epoch;
      std::unique_ptr<PreviewFrame> frame;
      try {
        frame = task->read();
      } catch (...) {
        // A failed preview must not lose the original scan or its final image.
      }
      task.reset(); // Drop the result reference before reporting reader idle.
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopped_ && epoch == epoch_ && frame)
          latest_ = std::move(frame);
        busy_ = false;
        cv_.notify_all();
      }
    }
  }

public:
  PreviewWorker() : thread_([this] { run(); }) {}
  ~PreviewWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
      pending_.reset();
      cv_.notify_all();
    }
    thread_.join();
  }
  bool request(std::function<std::unique_ptr<PreviewFrame>()> read,
               bool final = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || (!final && (busy_ || pending_)))
      return false;
    pending_.reset(new Task{epoch_, std::move(read)});
    cv_.notify_all();
    return true;
  }
  std::unique_ptr<PreviewFrame> take() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(latest_);
  }
  void cancelAndWait() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++epoch_;
    pending_.reset();
    latest_.reset();
    cv_.wait(lock, [this] { return !busy_; });
  }
};
} // namespace penchat_scan

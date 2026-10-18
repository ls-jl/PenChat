#pragma once
#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace penchat_scan {
// Bounded, lossless overflow queue. Only its worker performs file I/O; capture
// copies into a bounded inbox, while inference consumes prefetched frames.
class FrameQueue {
public:
  struct Frame {
    uint64_t sequence = 0;
    int64_t timestamp = 0;
    std::vector<uint8_t> pixels;
  };
  static constexpr size_t Pixels = 496 * 360;
  static constexpr size_t Capacity = 640, Inbox = 64, Ready = 8;

private:
  struct Header {
    uint64_t sequence;
    int64_t timestamp;
  };
  static constexpr size_t Record = sizeof(Header) + Pixels;
  int fd_ = -1;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Frame> inbox_, ready_;
  std::thread worker_;
  bool stopped_ = false;
  size_t pending_ = 0, peak_ = 0;
  uint64_t writes_ = 0;
  std::string error_;
  void transfer(void *data, size_t size, off_t offset, bool write) {
    auto p = static_cast<uint8_t *>(data);
    while (size) {
      ssize_t n =
          write ? pwrite(fd_, p, size, offset) : pread(fd_, p, size, offset);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("scan temporary queue I/O failed");
      p += n;
      size -= size_t(n);
      offset += n;
    }
  }
  void work() {
    uint64_t head = 0, tail = 0;
    try {
      for (;;) {
        Frame frame;
        bool load = false;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          condition_.wait(lock, [&] {
            return stopped_ || !inbox_.empty() ||
                   (head != tail && ready_.size() < Ready);
          });
          if (stopped_)
            return;
          if (head != tail && ready_.size() < Ready)
            load = true;
          else if (head == tail && ready_.size() < Ready) {
            ready_.push_back(std::move(inbox_.front()));
            inbox_.pop_front();
            continue;
          } else {
            frame = std::move(inbox_.front());
            inbox_.pop_front();
          }
        }
        if (load) {
          Header header{};
          const auto offset = off_t(head % Capacity) * Record;
          transfer(&header, sizeof(header), offset, false);
          frame.sequence = header.sequence;
          frame.timestamp = header.timestamp;
          frame.pixels.resize(Pixels);
          transfer(frame.pixels.data(), Pixels, offset + sizeof(header), false);
          ++head;
          std::lock_guard<std::mutex> lock(mutex_);
          ready_.push_back(std::move(frame));
        } else {
          Header header{frame.sequence, frame.timestamp};
          const auto offset = off_t(tail % Capacity) * Record;
          transfer(&header, sizeof(header), offset, true);
          transfer(frame.pixels.data(), Pixels, offset + sizeof(header), true);
          ++tail;
          std::lock_guard<std::mutex> lock(mutex_);
          ++writes_;
        }
      }
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(mutex_);
      error_ = e.what();
    }
  }

public:
  explicit FrameQueue(const std::string &directory) {
    auto name = directory + "/capture-XXXXXX";
    std::vector<char> path(name.begin(), name.end());
    path.push_back(0);
    fd_ = mkstemp(path.data());
    if (fd_ < 0)
      throw std::runtime_error("cannot create scan temporary queue");
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
    // No filesystem residue even after an unexpected process exit.
    unlink(path.data());
    try {
      worker_ = std::thread([this] { work(); });
    } catch (...) {
      close(fd_);
      throw;
    }
  }
  ~FrameQueue() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable())
      worker_.join();
    close(fd_);
  }
  void push(const uint8_t *pixels, int stride, uint64_t sequence,
            int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!error_.empty())
      throw std::runtime_error(error_);
    if (pending_ >= Capacity)
      throw std::runtime_error("scan temporary queue capacity");
    if (inbox_.size() >= Inbox)
      throw std::runtime_error("scan temporary queue storage stalled");
    Frame frame{sequence, timestamp, std::vector<uint8_t>(Pixels)};
    for (int row = 0; row < 360; ++row)
      memcpy(frame.pixels.data() + row * 496, pixels + row * stride, 496);
    inbox_.push_back(std::move(frame));
    ++pending_;
    peak_ = std::max(peak_, pending_);
    condition_.notify_one();
  }
  bool pop(Frame &frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!error_.empty())
      throw std::runtime_error(error_);
    if (ready_.empty())
      return false;
    frame = std::move(ready_.front());
    ready_.pop_front();
    --pending_;
    condition_.notify_one();
    return true;
  }
  size_t pending() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_;
  }
  size_t peak() {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
  }
  uint64_t spilled() {
    std::lock_guard<std::mutex> lock(mutex_);
    return writes_;
  }
};
} // namespace penchat_scan

#include "surface.hpp"
#include "composite_wire.hpp"
#include "display_surface.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace penchat_compositor {
namespace {
int configuredSocket = -1, configuredShared = -1;
class Surface final : public camera::DisplaySurface {
  int socket_ = -1, shared_ = -1;
  Geometry geometry_;
  const std::uint8_t* system_ = nullptr;
  std::uint8_t* output_ = nullptr;
  std::mutex mutex_;
  std::vector<std::uint8_t> native_;
  std::thread worker_;
  std::atomic<bool> quit_{false};
  bool visible_ = false;
  unsigned revision_ = 0;
  std::string error_;
  void send(const Packet& p) {
    if (!sendPacket(socket_, p)) throw std::runtime_error("S6 compositor connection lost");
  }
  void cleanup() {
    if (system_) munmap(const_cast<std::uint8_t*>(system_), 2 * geometry_.slotBytes);
    if (output_) munmap(output_, 2 * geometry_.slotBytes);
    system_ = nullptr; output_ = nullptr;
    if (socket_ >= 0) close(socket_);
    if (shared_ >= 0) close(shared_);
    socket_ = shared_ = -1;
  }
  void run() {
    try {
      bool free[2] = {true, true}, focused = false, announced = false;
      unsigned slotSequence[2] = {0, 0}, systemSequence = 0, epoch = 0, heartbeat = 0;
      unsigned outputSequence = 0, sentRevision = 0, sentSystem = 0, sentEpoch = 0, lastOutput = 0;
      int held = -1;
      while (!quit_) {
        pollfd wait{socket_, POLLIN, 0};
        const auto result = poll(&wait, 1, 8);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || wait.revents & (POLLHUP | POLLERR | POLLNVAL))
          throw std::runtime_error("S6 compositor disconnected");
        for (unsigned batch = 0; batch < 32; ++batch) {
          Packet p;
          const int status = receivePacket(socket_, p);
          if (!status) break;
          if (status < 0 || p.type == Wire::Stop) throw std::runtime_error("S6 compositor stopped");
          if (p.type == Wire::Focus) {
            if (p.value > 1 || !p.epoch) throw std::runtime_error("Invalid compositor focus");
            focused = p.value != 0; epoch = p.epoch; heartbeat = monotonicMs();
          } else if (p.type == Wire::System) {
            if (p.index >= 2 || !p.sequence || p.sequence <= systemSequence || int(p.index) == held)
              throw std::runtime_error("Invalid compositor system frame");
            if (held >= 0) {
              Packet release; release.type = Wire::ReleaseSystem; release.index = held; release.sequence = systemSequence; send(release);
            }
            held = p.index; systemSequence = p.sequence;
          } else if (p.type == Wire::ReleaseOutput) {
            if (p.index >= 2 || free[p.index] || slotSequence[p.index] != p.sequence)
              throw std::runtime_error("Invalid compositor output release");
            free[p.index] = true;
          } else throw std::runtime_error("Unexpected compositor packet");
        }
        const unsigned now = monotonicMs();
        if (std::uint32_t(now - heartbeat) > kDisplayLeaseMs) focused = false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (announced != visible_) {
          Packet p; p.type = Wire::Visibility; p.value = visible_ ? 1 : 0; send(p); announced = visible_;
        }
        const int slot = free[0] ? 0 : free[1] ? 1 : -1;
        if (!focused || !visible_ || !revision_ || held < 0 || slot < 0 ||
            (sentRevision == revision_ && sentSystem == systemSequence && sentEpoch == epoch) ||
            std::uint32_t(now - lastOutput) < 33) continue;
        const auto start = std::chrono::steady_clock::now();
        // Falcon's premultiplied system pixels are on top (including its hole).
        blendOver(system_ + held * geometry_.slotBytes, native_.data(), output_ + slot * geometry_.slotBytes, geometry_.bytes / 4);
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        Packet frame; frame.type = Wire::Output; frame.index = slot; frame.sequence = ++outputSequence;
        if (!frame.sequence) throw std::runtime_error("Compositor sequence exhausted");
        frame.epoch = epoch; frame.value = std::min<std::uint64_t>(micros, UINT32_MAX);
        frame.value2 = revision_; frame.value3 = systemSequence;
        send(frame); free[slot] = false; slotSequence[slot] = frame.sequence;
        sentRevision = revision_; sentSystem = systemSequence; sentEpoch = epoch; lastOutput = now;
      }
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!quit_) error_ = e.what();
    }
  }
public:
  Surface(const camera::DisplayGeometry& g, int socket, int shared) : socket_(socket), shared_(shared) {
    try {
      pollfd wait{socket_, POLLIN, 0}; Packet hello;
      if (socket_ < 0 || shared_ < 0 || poll(&wait, 1, 3000) <= 0 || receivePacket(socket_, hello) != 1 || hello.type != Wire::Hello)
        throw std::runtime_error("S6 compositor is unavailable");
      geometry_ = Geometry::make(g.width, g.height, sysconf(_SC_PAGESIZE));
      struct stat st{};
      if (hello.value != unsigned(g.width) || hello.aux != unsigned(g.height) || hello.value2 != geometry_.slotBytes ||
          fstat(shared_, &st) || std::uint64_t(st.st_size) != geometry_.total)
        throw std::runtime_error("S6 compositor geometry mismatch");
      auto* map = mmap(nullptr, 2 * geometry_.slotBytes, PROT_READ, MAP_SHARED, shared_, 0);
      if (map == MAP_FAILED) throw std::runtime_error("Cannot map compositor system frames");
      system_ = static_cast<const std::uint8_t*>(map);
      map = mmap(nullptr, 2 * geometry_.slotBytes, PROT_READ | PROT_WRITE, MAP_SHARED, shared_, 2 * geometry_.slotBytes);
      if (map == MAP_FAILED) throw std::runtime_error("Cannot map compositor output frames");
      output_ = static_cast<std::uint8_t*>(map);
      native_.resize(geometry_.bytes);
      send(hello);
      worker_ = std::thread([this] { run(); });
    } catch (...) { cleanup(); throw; }
  }
  ~Surface() override {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
    cleanup();
  }
  bool present(const std::uint8_t* pixels, int width, int height) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!error_.empty()) throw std::runtime_error(error_);
    if (!pixels || width != int(geometry_.width) || height != int(geometry_.height))
      throw std::runtime_error("S6 compositor frame geometry changed");
    std::memcpy(native_.data(), pixels, geometry_.bytes);
    if (!++revision_) throw std::runtime_error("Compositor revision exhausted");
    visible_ = true;
    // Accepted by bounded latest-frame storage. The startup cover is removed by
    // ready, then Falcon's next transparent frame permits physical composition.
    return true;
  }
  void hide() override { std::lock_guard<std::mutex> lock(mutex_); visible_ = false; }
  void check() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!error_.empty()) throw std::runtime_error(error_);
  }
  bool sharedCaptureSafe() const override { return true; }
  bool systemOverlaySafe() const override { return true; }
};
}
void configureDisplay(int socket, int shared) {
  if (socket < 0 || shared < 0 || socket == shared || configuredSocket >= 0 || configuredShared >= 0)
    throw std::runtime_error("Invalid S6 compositor descriptors");
  configuredSocket = socket; configuredShared = shared;
}
std::unique_ptr<camera::DisplaySurface> makeSurface(const camera::DisplayGeometry& g) {
  const int socket = configuredSocket, shared = configuredShared;
  configuredSocket = configuredShared = -1;
  return std::unique_ptr<camera::DisplaySurface>(new Surface(g, socket, shared));
}
}
namespace camera {
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry& g) {
  return penchat_compositor::makeSurface(g);
}
}

#include "composite.hpp"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
namespace submit_probe {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double median(const std::array<double, 64>& values, unsigned count) {
  if (!count) return 0;
  auto copy = values;
  const auto used = std::min(count, unsigned(copy.size()));
  std::sort(copy.begin(), copy.begin() + used);
  return copy[used / 2];
}
}
Composite& Composite::instance() { static auto* value = new Composite; return *value; }
bool Composite::freshFocus() const {
  return focused_.load() && std::uint32_t(monotonicMs() - focusAt_.load()) <= 300;
}
void Composite::start(const Slot& slot, int width, int height, const std::string& executable, unsigned seconds) {
  stop();
  if (seconds < 1 || seconds > 30 || !slot.address || !slot.original) throw std::runtime_error("Invalid composite lease");
  geometry_ = Geometry::make(width, height, sysconf(_SC_PAGESIZE));
  slot_ = slot; original_ = reinterpret_cast<Present>(slot.original);
  systemFrames_ = 0; outputFrames_ = 0; coalesced_ = 0; rejected_ = 0;
  nativeFrames_ = 0; taps_ = 0; epoch_ = 1; focused_ = false; focusAt_ = 0;
  systemSequence_ = 0; latestSystem_ = -1; pendingValid_ = false;
  freeSystem_[0] = freeSystem_[1] = true; slotSequence_[0] = slotSequence_[1] = 0;
  copyCount_ = frameCount_ = 0; reason_.clear(); quit_ = false; restored_ = true;
  started_ = monotonicMs(); ended_ = 0; deadline_ = seconds * 1000;
  try {
    shared_ = createShared(geometry_);
    auto* map = mmap(nullptr, 2 * geometry_.slotBytes, PROT_READ | PROT_WRITE, MAP_SHARED, shared_, 0);
    if (map == MAP_FAILED) throw std::runtime_error("Cannot map system slots");
    systems_ = static_cast<std::uint8_t*>(map);
    map = mmap(nullptr, 2 * geometry_.slotBytes, PROT_READ, MAP_SHARED, shared_, 2 * geometry_.slotBytes);
    if (map == MAP_FAILED) throw std::runtime_error("Cannot map output slots");
    outputs_ = static_cast<std::uint8_t*>(map);
    pending_.resize(geometry_.bytes);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair))
      throw std::runtime_error("Cannot create compositor channel");
    socket_ = pair[0];
    try { child_ = spawnClient(executable, pair[1], shared_); }
    catch (...) { close(pair[1]); throw; }
    close(pair[1]);
    Packet hello;
    hello.value = width; hello.aux = height; hello.value2 = geometry_.slotBytes;
    if (!sendPacket(socket_, hello)) throw std::runtime_error("Cannot configure compositor");
    pollfd p{socket_, POLLIN, 0};
    Packet ready;
    if (poll(&p, 1, 3000) <= 0 || receivePacket(socket_, ready) != 1 || ready.type != Wire::Hello ||
        ready.value != unsigned(width) || ready.aux != unsigned(height))
      throw std::runtime_error("Native compositor did not become ready");
    mode_ = Mode::Waiting; passthrough_ = false; restored_ = false;
    std::string error;
    if (!slot_.replace(slot.original, reinterpret_cast<std::uintptr_t>(&relay), error)) throw std::runtime_error(error);
    worker_ = std::thread([this] { loop(); });
  } catch (...) {
    mode_ = Mode::Draining; restore();
    while (callbacks_.load(std::memory_order_seq_cst)) usleep(1000);
    releaseResources(); mode_ = Mode::Detached;
    throw;
  }
}
bool Composite::send(const Packet& packet) {
  std::lock_guard<std::mutex> lock(channelMutex_);
  return socket_ >= 0 && sendPacket(socket_, packet);
}
void Composite::disconnect() { Packet p; p.type = Wire::Stop; send(p); }
void Composite::focus(bool active) {
  const bool wasFresh = freshFocus();
  focusAt_ = monotonicMs();
  if (focused_.exchange(active) != active || (active && !wasFresh)) ++epoch_;
  if (mode_ == Mode::Waiting || mode_ == Mode::Active) {
    Packet p; p.type = Wire::Focus; p.epoch = epoch_; p.value = active ? 1 : 0;
    if (!send(p)) quit_ = true;
  }
}
bool Composite::queueSystem(const std::uint8_t* source, unsigned sequence) {
  int index = freeSystem_[0] ? 0 : freeSystem_[1] ? 1 : -1;
  if (index < 0) return false;
  std::memcpy(systems_ + index * geometry_.slotBytes, source, geometry_.bytes);
  Packet p; p.type = Wire::System; p.index = index; p.sequence = sequence;
  if (!send(p)) return false;
  freeSystem_[index] = false; slotSequence_[index] = sequence; latestSystem_ = index;
  return true;
}
void Composite::submit(void* source, const Rect& dirty, int width, int height) {
  const auto state = mode_.load();
  bool accepted = false;
  try {
    if ((state == Mode::Waiting || state == Mode::Active) && source && width > 0 && height > 0 &&
        (width != int(geometry_.width) || height != int(geometry_.height))) {
      passthrough_ = true; mode_ = Mode::Draining; quit_ = true; ++rejected_;
    }
    if ((state == Mode::Waiting || state == Mode::Active) && source &&
        width == int(geometry_.width) && height == int(geometry_.height)) {
      const auto begin = Clock::now();
      {
        std::lock_guard<std::mutex> lock(systemMutex_);
        if (mode_ == Mode::Waiting || mode_ == Mode::Active) {
          const auto sequence = ++systemSequence_;
          if (!sequence) { quit_ = true; ++rejected_; }
          else {
            if (!queueSystem(static_cast<const std::uint8_t*>(source), sequence)) {
              std::memcpy(pending_.data(), source, geometry_.bytes);
              pendingSequence_ = sequence; pendingValid_ = true; ++coalesced_;
            } else pendingValid_ = false;
            ++systemFrames_; accepted = true;
          }
        }
      }
      std::unique_lock<std::mutex> stats(statsMutex_, std::try_to_lock);
      if (stats.owns_lock()) { copyTimes_[copyCount_ % copyTimes_.size()] = milliseconds(begin); ++copyCount_; }
    }
  } catch (...) {
    // Instrumentation must never throw across Falcon's display thread.
    passthrough_ = true; mode_ = Mode::Draining; quit_ = true; ++rejected_;
  }
  // Invalidate diagnostic consumption before the original may wait for vblank.
  callbacks_.fetch_sub(1, std::memory_order_seq_cst);
  if (accepted && freshFocus() && (mode_ == Mode::Waiting || mode_ == Mode::Active)) return;
  std::lock_guard<std::mutex> output(outputMutex_);
  passthrough_ = true;
  const auto original = original_.load();
  if (original) original(source, dirty, width, height);
}
void Composite::relay(void* source, const Rect& dirty, int width, int height) {
  auto& self = instance(); self.callbacks_.fetch_add(1, std::memory_order_seq_cst);
  self.submit(source, dirty, width, height);
}
void Composite::postLatestSystem() {
  std::lock_guard<std::mutex> output(outputMutex_);
  if (passthrough_.load()) return;
  std::lock_guard<std::mutex> system(systemMutex_);
  const std::uint8_t* source = pendingValid_ ? pending_.data() :
    latestSystem_ < 0 ? nullptr : systems_ + latestSystem_ * geometry_.slotBytes;
  if (source && original_.load()) {
    const Rect all{0, 0, int(geometry_.width), int(geometry_.height)};
    original_.load()(const_cast<std::uint8_t*>(source), all, geometry_.width, geometry_.height);
  }
}
void Composite::loop() {
  unsigned lastOutput = 0;
  bool lastFocus = false;
  std::string reason = "stopped";
  try {
    while (!quit_) {
      if (std::uint32_t(monotonicMs() - started_) >= deadline_) { reason = "deadline"; break; }
      const bool currentFocus = freshFocus();
      if (lastFocus && !currentFocus) { postLatestSystem(); passthrough_ = true; }
      lastFocus = currentFocus;
      pollfd p{socket_, POLLIN, 0};
      const auto ready = poll(&p, 1, 20);
      if (ready < 0 && errno == EINTR) continue;
      if (ready < 0 || p.revents & (POLLHUP | POLLERR | POLLNVAL)) { reason = "client_disconnected"; break; }
      if (!(p.revents & POLLIN)) continue;
      for (unsigned batch = 0; batch < 16; ++batch) {
        Packet packet;
        const auto result = receivePacket(socket_, packet);
        if (!result) break;
        if (result < 0) throw std::runtime_error("malformed_or_closed_channel");
        if (packet.type == Wire::ReleaseSystem) {
          std::lock_guard<std::mutex> system(systemMutex_);
          if (packet.index >= 2 || freeSystem_[packet.index] || slotSequence_[packet.index] != packet.sequence)
            throw std::runtime_error("invalid_system_release");
          freeSystem_[packet.index] = true;
          if (pendingValid_ && queueSystem(pending_.data(), pendingSequence_)) pendingValid_ = false;
        } else if (packet.type == Wire::Output) {
          if (packet.index >= 2 || !packet.sequence || packet.sequence <= lastOutput)
            throw std::runtime_error("invalid_output_frame");
          lastOutput = packet.sequence;
          if (freshFocus() && packet.epoch == epoch_.load()) {
            std::lock_guard<std::mutex> output(outputMutex_);
            if (freshFocus() && !quit_ && packet.epoch == epoch_.load()) {
              const auto begin = Clock::now();
              const Rect all{0, 0, int(geometry_.width), int(geometry_.height)};
              original_.load()(const_cast<std::uint8_t*>(outputs_ + packet.index * geometry_.slotBytes),
                               all, geometry_.width, geometry_.height);
              if (!quit_ && mode_ != Mode::Draining) mode_ = Mode::Active;
              passthrough_ = false; ++outputFrames_;
              taps_ = packet.aux; nativeFrames_ = packet.value2;
              std::lock_guard<std::mutex> stats(statsMutex_);
              blendTimes_[frameCount_ % blendTimes_.size()] = packet.value / 1000.0;
              presentTimes_[frameCount_ % presentTimes_.size()] = milliseconds(begin); ++frameCount_;
            }
          }
          packet.type = Wire::ReleaseOutput;
          if (!send(packet)) throw std::runtime_error("output_release_failed");
        } else throw std::runtime_error("unexpected_compositor_packet");
      }
    }
  } catch (const std::exception& e) { reason = e.what(); ++rejected_; }
  mode_ = Mode::Draining;
  postLatestSystem();
  restore();
  while (callbacks_.load(std::memory_order_seq_cst)) usleep(1000);
  releaseResources();
  {
    std::lock_guard<std::mutex> stats(statsMutex_);
    ended_ = monotonicMs(); reason_ = reason;
  }
  mode_ = Mode::Detached;
}
void Composite::restore() {
  std::lock_guard<std::mutex> lock(slotMutex_);
  if (restored_ || !slot_.address) return;
  if (__atomic_load_n(slot_.address, __ATOMIC_SEQ_CST) == slot_.original) { restored_ = true; return; }
  std::string error;
  restored_ = slot_.replace(reinterpret_cast<std::uintptr_t>(&relay), slot_.original, error);
}
void Composite::releaseResources() {
  { std::lock_guard<std::mutex> channel(channelMutex_);
  if (socket_ >= 0) { Packet p; p.type = Wire::Stop; sendPacket(socket_, p); shutdown(socket_, SHUT_RDWR); close(socket_); socket_ = -1; }
  }
  pid_t pid = child_.load(); reapClient(pid); child_ = pid;
  if (systems_) munmap(systems_, 2 * geometry_.slotBytes);
  if (outputs_) munmap(const_cast<std::uint8_t*>(outputs_), 2 * geometry_.slotBytes);
  systems_ = nullptr; outputs_ = nullptr;
  if (shared_ >= 0) close(shared_);
  shared_ = -1;
  std::fill(pending_.begin(), pending_.end(), 0);
  std::vector<std::uint8_t>().swap(pending_);
}
void Composite::stop() {
  mode_ = Mode::Draining; quit_ = true;
  if (worker_.joinable()) worker_.join();
  restore();
  while (callbacks_.load(std::memory_order_seq_cst)) usleep(1000);
  if (socket_ >= 0 || shared_ >= 0) releaseResources();
  mode_ = Mode::Detached;
}
CompositeStats Composite::stats() {
  std::lock_guard<std::mutex> stats(statsMutex_);
  CompositeStats s;
  s.running = mode_ == Mode::Waiting || mode_ == Mode::Active;
  s.originalRestored = restored_; s.focused = s.running && freshFocus(); s.child = child_;
  s.systemFrames = systemFrames_; s.outputFrames = outputFrames_; s.coalesced = coalesced_; s.rejected = rejected_;
  s.nativeFrames = nativeFrames_; s.taps = taps_; s.epoch = epoch_;
  s.elapsedMs = started_ ? std::uint32_t((ended_ ? ended_ : monotonicMs()) - started_) : 0;
  s.sharedBytes = geometry_.total;
  s.copyMedianMs = median(copyTimes_, copyCount_);
  s.blendMedianMs = median(blendTimes_, frameCount_); s.presentMedianMs = median(presentTimes_, frameCount_);
  s.reason = reason_;
  return s;
}
}

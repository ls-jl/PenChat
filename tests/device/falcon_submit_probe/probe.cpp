#include "probe.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

namespace submit_probe {
namespace {
using Clock = std::chrono::steady_clock;
void erase(std::vector<std::uint8_t>& buffer) {
  auto* p = static_cast<volatile std::uint8_t*>(buffer.data());
  for (std::size_t i = 0; i < buffer.size(); ++i) p[i] = 0;
  std::vector<std::uint8_t>().swap(buffer);
}
}
Probe& Probe::instance() {
  // The host can have a thread that already fetched the old GOT value when we
  // restore it. Keep relay state alive until process exit; the plugin is pinned.
  static Probe* state = new Probe;
  return *state;
}
void Probe::start(const Slot& slot, int width, int height, bool copy, unsigned seconds, unsigned limit) {
  stop();
  if (width <= 0 || height <= 0 || std::uint64_t(width) * height > 4 * 1024 * 1024 ||
      seconds < 1 || seconds > 30 || !limit || limit > 64 || !slot.original || !slot.address)
    throw std::runtime_error("Invalid bounded probe configuration");
  std::lock_guard<std::mutex> control(control_);
  slot_ = slot;
  original_.store(reinterpret_cast<Present>(slot.original), std::memory_order_seq_cst);
  calls_ = 0; invalid_ = 0; busy_ = 0;
  {
    std::lock_guard<std::mutex> samples(samplesMutex_);
    width_ = width; height_ = height; copy_ = copy; limit_ = limit;
    samples_.clear(); samples_.reserve(limit);
    if (copy) scratch_.resize(std::size_t(width) * height * 4);
  }
  error_.clear(); restored_ = false;
  enabled_.store(true, std::memory_order_seq_cst);
  if (!slot_.replace(slot.original, reinterpret_cast<std::uintptr_t>(&relay), error_)) {
    enabled_.store(false, std::memory_order_seq_cst);
    restored_ = __atomic_load_n(slot.address, __ATOMIC_SEQ_CST) == slot.original;
    throw std::runtime_error(error_);
  }
  try {
    const auto deadline = Clock::now() + std::chrono::seconds(seconds);
    watchdog_ = std::thread([this, deadline] {
      while (enabled_.load(std::memory_order_seq_cst) && Clock::now() < deadline) usleep(10000);
      restore();
    });
  } catch (...) {
    enabled_.store(false, std::memory_order_seq_cst);
    std::string ignored;
    restored_ = slot_.replace(reinterpret_cast<std::uintptr_t>(&relay), slot_.original, ignored);
    throw;
  }
}
void Probe::restore() {
  std::lock_guard<std::mutex> control(control_);
  enabled_.store(false, std::memory_order_seq_cst);
  if (!slot_.address || restored_) return;
  if (__atomic_load_n(slot_.address, __ATOMIC_SEQ_CST) == slot_.original) {
    restored_ = true; return;
  }
  std::string error;
  restored_ = slot_.replace(reinterpret_cast<std::uintptr_t>(&relay), slot_.original, error);
  if (!restored_) error_ = error;
}
void Probe::stop() {
  restore();
  if (watchdog_.joinable()) watchdog_.join();
  // No callback that sees disabled can enter observe. An old fetched relay
  // remains a transparent call to original even after this wait.
  while (inFlight_.load(std::memory_order_seq_cst)) usleep(1000);
  std::lock_guard<std::mutex> samples(samplesMutex_);
  erase(scratch_);
}
void Probe::observe(void* source, int width, int height) {
  if (!source || width <= 0 || height <= 0 ||
      !((width == width_ && height == height_) || (width == height_ && height == width_))) {
    invalid_.fetch_add(1, std::memory_order_relaxed); return;
  }
  if (!copy_) return;
  std::unique_lock<std::mutex> lock(samplesMutex_, std::try_to_lock);
  if (!lock.owns_lock()) { busy_.fetch_add(1, std::memory_order_relaxed); return; }
  if (samples_.size() >= limit_ || scratch_.empty()) return;
  // displayLoop owns the SurfaceBuffer until post_fb returns. Its current ABI
  // uses packed width*height*4 pixels, as the original copy/rotate paths do.
  const auto begin = Clock::now();
  std::memcpy(scratch_.data(), source, scratch_.size());
  asm volatile("" ::: "memory");
  const auto end = Clock::now();
  samples_.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
}
void Probe::relay(void* source, const Rect& dirty, int width, int height) {
  auto& state = instance();
  state.inFlight_.fetch_add(1, std::memory_order_seq_cst);
  const auto original = state.original_.load(std::memory_order_seq_cst);
  state.calls_.fetch_add(1, std::memory_order_relaxed);
  if (state.enabled_.load(std::memory_order_seq_cst)) state.observe(source, width, height);
  // Never substitute the diagnostic copy, change dirty geometry, rotate, call
  // the framebuffer, or submit twice. Preserve every original argument.
  state.inFlight_.fetch_sub(1, std::memory_order_seq_cst);
  if (original) original(source, dirty, width, height);
}
Stats Probe::stats() {
  std::lock_guard<std::mutex> control(control_);
  Stats out;
  out.installed = enabled_.load(std::memory_order_seq_cst);
  out.calls = calls_; out.invalid = invalid_; out.busy = busy_;
  out.originalRestored = restored_; out.error = error_;
  std::lock_guard<std::mutex> samples(samplesMutex_);
  out.width = width_; out.height = height_;
  out.bytes = std::size_t(width_) * height_ * 4;
  out.samples = samples_.size();
  auto sorted = samples_;
  std::sort(sorted.begin(), sorted.end());
  if (!sorted.empty()) {
    out.copyMedianMs = sorted[sorted.size() / 2];
    out.copyP95Ms = sorted[(sorted.size() * 95 + 99) / 100 - 1];
    out.copyMaxMs = sorted.back();
  }
  return out;
}
}

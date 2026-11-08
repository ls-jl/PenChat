#pragma once
#include "elf_slot.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace submit_probe {
struct Rect { int left, top, right, bottom; };
using Present = void (*)(void*, const Rect&, int, int);
struct Stats {
  bool installed = false;
  std::uint32_t calls = 0, samples = 0, invalid = 0, busy = 0;
  std::uint32_t width = 0, height = 0;
  std::size_t bytes = 0;
  double copyMedianMs = 0, copyP95Ms = 0, copyMaxMs = 0;
  bool originalRestored = true;
  std::string error;
};
class Probe {
public:
  static Probe& instance();
  void start(const Slot&, int width, int height, bool copy, unsigned seconds, unsigned sampleLimit);
  void stop();
  Stats stats();
  static void relay(void*, const Rect&, int, int);
private:
  void restore();
  void observe(void*, int, int);
  std::mutex control_, samplesMutex_;
  Slot slot_;
  std::atomic<Present> original_{nullptr};
  std::atomic<bool> enabled_{false};
  std::atomic<std::uint32_t> inFlight_{0}, calls_{0}, invalid_{0}, busy_{0};
  std::thread watchdog_;
  std::vector<std::uint8_t> scratch_;
  std::vector<double> samples_;
  int width_ = 0, height_ = 0;
  bool copy_ = false, restored_ = true;
  unsigned limit_ = 0;
  std::string error_;
};
}

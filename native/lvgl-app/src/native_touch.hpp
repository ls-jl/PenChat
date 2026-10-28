#pragma once
#include "common.hpp"
namespace penchat {
inline std::pair<int, int> logicalTouch(const Screen &s, int rawX, int rawY) {
  const int x = rawX - s.touchXoffset, y = rawY - s.touchYoffset;
  if (s.touchRotation == 270) return {s.width - 1 - y, x};
  if (s.touchRotation == 180) return {s.width - 1 - x, s.height - 1 - y};
  if (s.touchRotation == 90) return {y, s.height - 1 - x};
  return {x, y};
}

// Type-B frames are committed only at SYN_REPORT, including nonzero slots.
// The decoder is independent of the fd so tests exercise the production parser.
class TouchFrames {
public:
  struct Slot { int id = -1, x = 0, y = 0; };
  explicit TouchFrames(const Screen &screen) : screen_(screen) {}
  void reset(const std::vector<Slot> &slots, int selected = 0, bool wait = false);
  std::vector<Json> event(int type, int code, int value);
  bool dropped() const { return dropped_; }
  Json cancel();
private:
  Screen screen_;
  std::vector<Slot> slots_;
  Json previous_ = Json::array();
  int selected_ = 0, lastX_ = 0, lastY_ = 0;
  bool waitClear_ = false, dropped_ = false;
  uint64_t serial_ = uint64_t(1) << 48;
  Json packet(const Json &points, bool cancelled = false);
};

// Foreground-only, read-only and nonexclusive. Never grab the system device.
class NativeTouch {
  Screen screen_;
  TouchFrames frames_;
  int fd_ = -1;
  bool enabled_ = false;
  std::string device_;
  void open();
  void resync(bool wait);
public:
  NativeTouch(const Screen &screen, bool enabled) : screen_(screen), frames_(screen), enabled_(enabled) {}
  ~NativeTouch() { close(); }
  void close();
  bool enabled() const { return enabled_; }
  bool active() const { return fd_ >= 0; }
  Json diagnostics() const;
  std::vector<Json> poll(bool foreground);
  void wait(unsigned milliseconds) const;
};
} // namespace penchat

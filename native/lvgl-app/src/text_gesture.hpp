#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace penchat {
// All coordinates here are logical screen pixels, before editor design scaling.
struct TextGesture {
  enum Mode { Idle, Pending, Held, Scroll, Caret, Range, Handle };
  Mode mode = Idle;
  int x = 0, y = 0, lastX = 0, lastY = 0, distance = 0;
  int tapX = 0, tapY = 0;
  int64_t began = 0, tapped = -1000;
  bool writable = false;
  void begin(int px, int py, int64_t now, bool edit, bool handle = false) {
    x = lastX = px;
    y = lastY = py;
    distance = 0;
    began = now;
    writable = edit;
    mode = handle ? Handle : Pending;
  }
  bool hold(int64_t now) {
    if (mode != Pending || now - began < 450)
      return false;
    mode = Held;
    return true;
  }
  void move(int px, int py, int64_t now) {
    hold(now);
    lastX = px;
    lastY = py;
    distance = std::max(distance, std::max(std::abs(px - x), std::abs(py - y)));
    if (distance > 12) {
      if (mode == Pending)
        mode = Scroll;
      else if (mode == Held)
        mode = writable ? Caret : Range;
    }
    if (mode == Scroll || mode == Caret || mode == Range || mode == Handle)
      tapped = -1000;
  }
  bool doubleTap(int px, int py, int64_t now) {
    const bool twice = now - tapped < 350 && std::abs(px - tapX) <= 12 && std::abs(py - tapY) <= 12;
    tapped = twice ? -1000 : now;
    tapX = px;
    tapY = py;
    return twice;
  }
  bool active() const { return mode != Idle; }
  void end() { mode = Idle; }
  void cancel() {
    mode = Idle;
    tapped = -1000;
  }
};
} // namespace penchat

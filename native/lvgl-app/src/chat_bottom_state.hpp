#pragma once
#include <cstdint>

namespace penchat {
// Visual state only. Actual bottom/following and pull-to-create keep using the
// measured scroll position. A short excursion must not move either control.
class ChatBottomState {
public:
  bool atBottom() const { return atBottom_; }
  void reset(bool atBottom) {
    initialized_ = true;
    atBottom_ = atBottom;
    pause();
  }
  void pause() { pending_ = false; }
  bool update(bool latestPage, int bottom, int enter, int leave, int64_t now) {
    const bool measured = latestPage && bottom <= enter;
    if (!initialized_) {
      reset(measured);
      return true;
    }
    const bool target = latestPage && bottom <= (atBottom_ ? leave : enter);
    if (target == atBottom_) {
      pause();
      return false;
    }
    if (!pending_ || target != pendingBottom_ || now < pendingAt_) {
      pending_ = true;
      pendingBottom_ = target;
      pendingAt_ = now;
      return false;
    }
    if (now - pendingAt_ < 120) return false;
    atBottom_ = target;
    pause();
    return true;
  }
private:
  bool initialized_ = false, atBottom_ = true;
  bool pending_ = false, pendingBottom_ = false;
  int64_t pendingAt_ = 0;
};
} // namespace penchat

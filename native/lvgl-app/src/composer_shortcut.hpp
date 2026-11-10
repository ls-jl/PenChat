#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
namespace penchat {
// Temporary reader UI state only: never persists a draft or changes following.
class ComposerShortcut {
public:
  enum class Mode { Automatic, Manual, Off };
  void configure(const std::string &value) {
    mode_ = value == "off" ? Mode::Off : value == "manual" ? Mode::Manual : Mode::Automatic;
    reset();
  }
  const char *mode() const { return mode_ == Mode::Off ? "off" : mode_ == Mode::Manual ? "manual" : "auto"; }
  bool enabled() const { return mode_ != Mode::Off; }
  bool tracking() const { return down_; }
  bool shown() const { return shown_; }
  int64_t distance() const { return distance_; }
  int64_t limit() const { return limit_; }
  void pause() { down_ = moved_ = false; tapAt_ = 0; }
  void reset() { pause(); shown_ = false; distance_ = limit_ = 0; }
  bool touch(int x, int y, bool pressed, bool cancelled, bool inside, int64_t now, int height) {
    if (!down_) {
      if (!enabled() || !inside || !pressed || cancelled) return false;
      down_ = true; moved_ = false; startX_ = x; startY_ = y; startAt_ = now;
    }
    const int64_t dx = int64_t(x) - startX_, dy = int64_t(y) - startY_;
    moved_ = moved_ || dx * dx + dy * dy > 144;
    if (!pressed || cancelled) {
      down_ = false;
      if (cancelled || moved_ || !inside || now - startAt_ > 450) {
        tapAt_ = 0;
      } else {
        const int64_t tx = int64_t(x) - tapX_, ty = int64_t(y) - tapY_;
        if (tapAt_ && now - tapAt_ <= 350 && tx * tx + ty * ty <= 144) {
          shown_ = true; distance_ = 0; limit_ = int64_t(height) * 3 / 2; tapAt_ = 0;
        } else { tapAt_ = now; tapX_ = x; tapY_ = y; }
      }
    }
    return true;
  }
  void tick(int64_t now) {
    if (!down_ && tapAt_ && now - tapAt_ > 350) { shown_ = false; tapAt_ = 0; }
  }
  void scroll(int delta) {
    if (!shown_ || mode_ != Mode::Automatic) return;
    distance_ += delta < 0 ? -int64_t(delta) : int64_t(delta);
    if (distance_ > limit_) { shown_ = false; tapAt_ = 0; }
  }
private:
  Mode mode_ = Mode::Automatic;
  bool shown_ = false, down_ = false, moved_ = false;
  int startX_ = 0, startY_ = 0, tapX_ = 0, tapY_ = 0;
  int64_t startAt_ = 0, tapAt_ = 0, distance_ = 0, limit_ = 0;
};
} // namespace penchat

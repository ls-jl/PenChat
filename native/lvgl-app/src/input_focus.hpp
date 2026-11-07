#pragma once
#include <cstdint>
namespace penchat {
// Window visibility alone does not exclude system overlays. The launcher renews
// this short lease only after checking the actual Falcon window focus. Expiry
// fails closed if its JS/UI thread is blocked or the bridge stops reporting.
class InputFocus {
  bool required_ = false, active_ = false;
  std::uint64_t sequence_ = 0;
  std::int64_t renewed_ = 0;
public:
  explicit InputFocus(bool required = false) : required_(required) {}
  bool update(std::uint64_t sequence, bool active, std::int64_t now) {
    if(sequence == 0 || sequence <= sequence_) return false;
    required_ = true; sequence_ = sequence; active_ = active; renewed_ = now;
    return true;
  }
  void block() { active_ = false; }
  bool allowed(std::int64_t now) const {
    return !required_ || (active_ && now >= renewed_ && now - renewed_ < 300);
  }
};
}

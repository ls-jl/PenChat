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
    return valid(now,300);
  }
  struct Permissions { bool input,media,display; };
  Permissions permissions(std::int64_t now,bool page,bool window,bool official,
                          bool partialOfficial,bool retainUnderOverlay,bool firstFrame) const {
    const bool media=page&&window&&!official&&mediaAllowed(now);
    return {media&&allowed(now),media,
      page&&((!official&&(media||retainUnderOverlay||firstFrame)) || (partialOfficial&&window))};
  }
  // Do not tear down/rearm media for a single slow native draw. Input still
  // expires at 300ms; explicit focus loss revokes both immediately.
  bool mediaAllowed(std::int64_t now) const {return valid(now,1500);}
private:
  bool valid(std::int64_t now,std::int64_t duration) const {
    return !required_ || (active_ && now >= renewed_ && now - renewed_ < duration);
  }
};
}

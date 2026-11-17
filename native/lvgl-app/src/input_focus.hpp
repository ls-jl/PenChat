#pragma once
#include <cstdint>
#include <array>
#include <cstddef>
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
  struct Snapshot { bool required,active; std::uint64_t sequence; std::int64_t age; };
  Snapshot snapshot(std::int64_t now) const { return {required_,active_,sequence_,required_?now-renewed_:-1}; }
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
                          bool partialOfficial,bool retainUnderOverlay) const {
    // A fresh foreground heartbeat may display before nativeReady grants input.
    // Keep that display lease across the ready/active round-trip to avoid flicker.
    // partialOfficial is an independently validated, expiring display-only IME lease.
    const bool media=page&&window&&!official&&mediaAllowed(now);
    return {media&&allowed(now),media,
      page&&((!official&&(media||retainUnderOverlay||(window&&fresh(now)))) || (partialOfficial))};
  }
  // Do not tear down/rearm media for a single slow native draw. Input still
  // expires at 300ms; explicit focus loss revokes both immediately.
  bool mediaAllowed(std::int64_t now) const {return valid(now,1500);}
private:
  bool fresh(std::int64_t now) const { return !required_||(sequence_&&now>=renewed_&&now-renewed_<300); }
  bool valid(std::int64_t now,std::int64_t duration) const {
    return !required_ || (active_ && now >= renewed_ && now - renewed_ < duration);
  }
};
// Temporary numeric-only diagnostics. Recording never allocates or performs IO.
class InputFocusTrace {
public:
  enum Reason { InputApplied, WindowReceived, OverlayApplied, TouchApplied };
  struct Entry {
    std::int64_t time=0,age=-1;
    std::uint64_t sequence=0,eventSequence=0;
    Reason reason=InputApplied;
    bool value=false,required=false,active=false,page=false,window=false,official=false,touch=false;
    unsigned overlayKnown=0,overlayValues=0;
  };
  static constexpr std::size_t Capacity=64;
  void record(const Entry &entry) { entries_[next_]=entry;next_=(next_+1)%Capacity;if(size_<Capacity)++size_;++total_; }
  std::size_t size() const { return size_; }
  std::uint64_t total() const { return total_; }
  const Entry &at(std::size_t index) const { return entries_[(next_+Capacity-size_+index)%Capacity]; }
private:
  std::array<Entry,Capacity> entries_{};
  std::size_t next_=0,size_=0;
  std::uint64_t total_=0;
};
}

#pragma once
#include <cstddef>
#include <cstdint>

namespace camera { namespace cvi {
// Each slot has one application reference. A larger count means VO still owns
// its pixels; neither queued nor currently displayed frames may be overwritten.
// Query is supplied by the public CVI_VB_InquireUserCnt API, not a guessed delay.
template<std::size_t Count> class FrameSlots {
  static_assert(Count > 0, "At least one frame slot is required");
  std::size_t next_ = 0;
public:
  std::uint64_t busyChecks = 0, deferred = 0;
  template<class Query> int writable(Query query) {
    for(std::size_t n = 0; n < Count; ++n) {
      const auto index = (next_ + n) % Count;
      if(query(index) == 1) {
        next_ = (index + 1) % Count;
        return int(index);
      }
      ++busyChecks;
    }
    ++deferred;
    return -1;
  }
};
}}

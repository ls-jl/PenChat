#pragma once
#include <cstdint>

namespace penchat {
namespace refresh {
// Geometry is the interpreted Falcon logical screen, before any editor clipping.
inline int defaultRate(int width, int height) { return height > width ? 60 : 30; }
inline bool supportedRate(int rate) { return rate == 30 || rate == 60; }
inline int resolveRate(int rate, int width, int height) {
  return supportedRate(rate) ? rate : defaultRate(width, height);
}
// Millisecond timer targets; actual output still depends on rendering/host load.
inline std::uint32_t periodMs(int rate) { return rate == 60 ? 16u : 33u; }

// Keep the previous presentation time when changing rate. A new setting must
// neither queue old frames nor reset the compositor's slot/visibility ownership.
class Pacing {
  std::uint32_t last_ = 0;
  bool started_ = false;
public:
  std::uint32_t remainingMs(std::uint32_t now, int rate) const {
    if (!started_) return 0;
    const auto elapsed = std::uint32_t(now - last_);
    const auto period = periodMs(rate);
    return elapsed < period ? period - elapsed : 0;
  }
  bool due(std::uint32_t now, int rate) const {
    return remainingMs(now, rate) == 0;
  }
  void presented(std::uint32_t now) { last_ = now; started_ = true; }
};
} // namespace refresh
} // namespace penchat

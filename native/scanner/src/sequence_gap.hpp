#pragma once
#include <cstdint>
#include <string>

namespace penchat_scan {
inline bool recoverableSequenceGap(const std::string &profile, uint32_t distance,
                                  int64_t elapsedUs, unsigned stableFrames) {
  // Missing count is distance - 1: Y02 stops at eight consecutive missing
  // frames, not eight over an entire scan. Keep timestamp/reset checks; do not
  // let the former 40ms / stable-frame rules stop a smaller Y02 gap.
  if (profile == "y02-sc035gs")
    return distance >= 2 && distance <= 8 && elapsedUs > 0;
  return distance == 2 && elapsedUs > 0 && elapsedUs <= 35000 &&
         stableFrames >= 16;
}
} // namespace penchat_scan

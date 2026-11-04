#pragma once
#include <cstdint>
#include <string>

namespace penchat_scan {
inline bool recoverableSequenceGap(const std::string &profile, uint32_t distance,
                                  int64_t elapsedUs, unsigned stableFrames) {
  // SC035GS is measured at ~8.4ms/frame. A short 2-3 frame burst can retain
  // enough overlap for matching; resets, long gaps and repeated loss still stop.
  const uint32_t maxMissing = profile == "y02-sc035gs" ? 3 : 1;
  const int64_t maxElapsedUs = profile == "y02-sc035gs" ? 40000 : 35000;
  return distance >= 2 && distance <= maxMissing + 1 && elapsedUs > 0 &&
         elapsedUs <= maxElapsedUs && stableFrames >= 16;
}
} // namespace penchat_scan

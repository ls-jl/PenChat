#pragma once
#include <cstdint>
namespace penchat_scan {
// Sequence distance includes the received frame: distance 9 means 8 missing.
// Small gaps accumulate diagnostics, never a whole-scan stopping threshold.
inline bool recoverableSequenceGap(uint32_t distance, int64_t elapsedUs) {
  return distance >= 2 && distance <= 9 && elapsedUs > 0;
}
} // namespace penchat_scan

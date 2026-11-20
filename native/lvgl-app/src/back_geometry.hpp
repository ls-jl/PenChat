#pragma once
#include "ui_metrics.hpp"
#include <cmath>

namespace penchat {
struct BackGeometry {
  int edge, slop, distance;
};
inline BackGeometry backGeometry(int logicalWidth, const UiMetrics &metrics) {
  // Keep an inner start available beside the system's outer-edge preview.
  // Only the start target grows; claiming and completing retain their travel.
  return {std::max(metrics.touchTarget(20), int(std::lround(logicalWidth * .046))),
          std::max(metrics.dp(8), int(std::lround(logicalWidth * .0125))),
          std::max(metrics.dp(44), int(std::lround(logicalWidth * .10)))};
}
} // namespace penchat

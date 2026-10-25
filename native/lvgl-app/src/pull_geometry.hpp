#pragma once
#include <algorithm>
#include <cmath>
namespace penchat {
// Half-open bounds in Falcon's already rotated/mapped logical page coordinates.
struct PullRect {
  int left, top, right, bottom;
};
inline int pullSensitivityOffset(int percent) {
  return std::max(-25, std::min(25, percent));
}
struct PullGeometry {
  PullRect reader{0, 0, 0, 0};
  int sensitivityOffset = 0;
  int start = 12, fill = 155, sideways = 18, slowDistance = 40, slowSpeed = 260,
      stretch = 110;
  bool contains(int x, int y) const {
    return x >= reader.left && x < reader.right && y >= reader.top &&
           y < reader.bottom;
  }
};
inline PullGeometry pullGeometry(PullRect body, int width, int height,
                                 int composerTop, int offset = 0) {
  PullGeometry g;
  g.reader = {std::max(0, body.left), std::max(0, body.top),
              std::min(width, body.right),
              std::min({height, body.bottom, composerTop})};
  const double scale = std::max(0, g.reader.bottom - g.reader.top) / 208.0;
  auto scaled = [scale](double v) {
    return std::max(1, int(std::lround(v * scale)));
  };
  g.sensitivityOffset = pullSensitivityOffset(offset);
  // Lower baseline sensitivity to 85%; signed offsets apply to that new base.
  // Slop/direction/slow-pull guards keep their existing spatial behavior.
  const double sensitivity = .85 * (1.0 + g.sensitivityOffset / 100.0);
  g.start = scaled(12);
  g.fill = std::max(1, scaled(142 / sensitivity) - g.start);
  g.sideways = scaled(18);
  g.slowDistance = scaled(40);
  g.slowSpeed = scaled(260);
  g.stretch = scaled(110);
  return g;
}
} // namespace penchat

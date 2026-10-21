#pragma once
#include <algorithm>
#include <cmath>
namespace penchat {
// Half-open bounds in Falcon's already rotated/mapped logical page coordinates.
struct PullRect {
  int left, top, right, bottom;
};
struct PullGeometry {
  PullRect reader{0, 0, 0, 0};
  int start = 12, fill = 130, sideways = 18, slowDistance = 40, slowSpeed = 260,
      stretch = 110;
  bool contains(int x, int y) const {
    return x >= reader.left && x < reader.right && y >= reader.top &&
           y < reader.bottom;
  }
};
inline PullGeometry pullGeometry(PullRect body, int width, int height,
                                 int composerTop) {
  PullGeometry g;
  g.reader = {std::max(0, body.left), std::max(0, body.top),
              std::min(width, body.right),
              std::min({height, body.bottom, composerTop})};
  const double scale = std::max(0, g.reader.bottom - g.reader.top) / 208.0;
  auto scaled = [scale](double v) {
    return std::max(1, int(std::lround(v * scale)));
  };
  g.start = scaled(12);
  g.fill = std::max(1, scaled(142) - g.start);
  g.sideways = scaled(18);
  g.slowDistance = scaled(40);
  g.slowSpeed = scaled(260);
  g.stretch = scaled(110);
  return g;
}
} // namespace penchat

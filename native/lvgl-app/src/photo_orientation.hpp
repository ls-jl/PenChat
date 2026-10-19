#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace penchat {
// Ported from Camera's auto-orientation.js: projected gravity, smoothing and
// 600 ms stability avoid changing pose on flat placement or a transient shake.
struct PhotoOrientation {
  double x = 0, y = 0, z = 0;
  int candidate = -1;
  int64_t since = 0, last = 0;
  bool valid = false;
  static int classify(double x, double y, double z) {
    const double ax = std::abs(x) / 1024., ay = std::abs(y) / 1024.;
    const double projection = std::sqrt(ax * ax + ay * ay);
    if (std::abs(z) > 4096 || projection < .45 || projection > 1.4)
      return -1;
    if (ax > ay * 1.5 && ax - ay > .2)
      return x > 0 ? 0 : 180;
    if (ay > ax * 1.5 && ay - ax > .2)
      return y > 0 ? 90 : 270;
    return -1;
  }
  int update(int sx, int sy, int sz, int64_t now, int current) {
    if (now < last || now - last > 1000) {
      valid = false;
      candidate = -1;
    }
    last = now;
    if (classify(sx, sy, sz) < 0) {
      valid = false;
      candidate = -1;
      return current;
    }
    if (!valid) {
      x = sx;
      y = sy;
      z = sz;
      valid = true;
    } else {
      x += (sx - x) * .45;
      y += (sy - y) * .45;
      z += (sz - z) * .45;
    }
    int next = classify(x, y, z);
    if (next < 0 || next == current) {
      candidate = -1;
      return current;
    }
    if (next != candidate) {
      candidate = next;
      since = now;
      return current;
    }
    if (now - since < 600)
      return current;
    candidate = -1;
    return next;
  }
};
inline std::vector<uint8_t> rotatePhoto(const std::vector<uint8_t> &src, int w,
                                        int h, int angle) {
  const int ow = angle % 180 ? h : w;
  std::vector<uint8_t> out(src.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int dx = x, dy = y;
      if (angle == 90) {
        dx = h - 1 - y;
        dy = x;
      } else if (angle == 180) {
        dx = w - 1 - x;
        dy = h - 1 - y;
      } else if (angle == 270) {
        dx = y;
        dy = w - 1 - x;
      }
      for (int c = 0; c < 3; ++c)
        out[(size_t(dy) * ow + dx) * 3 + c] = src[(size_t(y) * w + x) * 3 + c];
    }
  return out;
}
} // namespace penchat

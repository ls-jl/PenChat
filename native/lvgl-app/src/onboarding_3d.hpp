#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace penchat {
// Bounded software 3D for the synthetic tutorial only. No display/GPU ownership.
class Onboarding3D {
public:
  struct Texture { const uint8_t *pixels; int width, height, stride; };
  struct Point { float x, y, z, u = 0, v = 0; };
  struct Projected { float x, y, inverseZ, u, v; };
  Onboarding3D(int width, int height);
  void render(Texture texture, int page, int phase, float progress,
              uint32_t background, uint32_t accent);
  const uint32_t *pixels() const { return pixels_.data(); }
  int width() const { return width_; }
  int height() const { return height_; }
  size_t bytes() const { return pixels_.size()*sizeof(uint32_t)+depth_.size()*sizeof(float); }
  unsigned triangles() const { return triangles_; }
  unsigned frames() const { return frames_; }
  static Projected project(Point p, int width, int height, float scale);
private:
  int width_, height_;
  float scale_ = 1, pitch_ = 0, yaw_ = 0;
  std::vector<uint32_t> pixels_;
  std::vector<float> depth_;
  unsigned triangles_ = 0, frames_ = 0;
  Point transform(Point) const;
  void triangle(Point, Point, Point, uint32_t, const Texture * = nullptr);
  void quad(Point, Point, Point, Point, uint32_t, const Texture * = nullptr);
  void panel(Texture);
  void pen(float, float, uint32_t);
};
} // namespace penchat

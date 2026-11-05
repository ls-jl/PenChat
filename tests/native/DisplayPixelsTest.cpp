#include "camera_logic.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
static std::vector<uint8_t> pixels(int w, int h) {
  std::vector<uint8_t> p(size_t(w) * h * 4);
  for (size_t i = 0; i < p.size(); i += 4) {
    p[i] = uint8_t(i * 7); p[i+1] = uint8_t(i * 11 + 1);
    p[i+2] = uint8_t(i * 13 + 2); p[i+3] = uint8_t(i * 17 + 3);
  }
  return p;
}
static void regression() {
  const int w = 73, h = 41, dw = 101, dh = 89;
  const auto bgra = pixels(w, h);
  std::vector<uint8_t> rgba(bgra.size()), oldPath(dw*dh*4), direct(oldPath.size());
  camera::rgbaToBgra(bgra.data(), rgba.data(), w*h);
  for (int angle : {0, 90, 180, 270}) {
    for (int x : {-20, 0, 13}) for (int y : {-15, 0, 9}) {
      // Reference is the previous complete BGRA->RGBA->resample->BGRA path,
      // including nonzero clipping, transparent pixels and non-square sources.
      require(camera::resampleRgbaViewport(rgba.data(), w, h, oldPath.data(), dw, dh, x, y, 87, 62, angle), "old upload");
      camera::rgbaToBgra(oldPath.data(), oldPath.data(), dw*dh);
      require(camera::resampleRgbaViewport(bgra.data(), w, h, direct.data(), dw, dh, x, y, 87, 62, angle), "direct upload");
      require(direct == oldPath, "rotation/clipping/alpha/color regression");
    }
  }
  const uint8_t redBlue[] = {0, 0, 255, 127, 255, 0, 0, 255}; // DRM BGRA
  uint8_t screenshot[8]; camera::rgbaToBgra(redBlue, screenshot, 2);
  require(screenshot[0] == 255 && screenshot[2] == 0 && screenshot[3] == 127 &&
          screenshot[4] == 0 && screenshot[6] == 255 && screenshot[7] == 255,
          "PNG gets RGBA with exact red/blue/alpha");
}
static void benchmark(int w, int h) {
  const auto bgra = pixels(w, h);
  std::vector<uint8_t> rgba(bgra.size()), out(bgra.size());
  auto run = [&](bool legacy) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 12; ++i) {
      const uint8_t *src = bgra.data();
      if (legacy) { camera::rgbaToBgra(src, rgba.data(), w*h); src = rgba.data(); }
      camera::resampleRgbaViewport(src, w, h, out.data(), w, h, 0, 0, w, h, 0);
      if (legacy) camera::rgbaToBgra(out.data(), out.data(), w*h);
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()/12;
  };
  const auto before = run(true), after = run(false);
  require(out == bgra, "direct full-screen pixels unchanged");
  std::cout << "DISPLAY_US " << w << 'x' << h << " old=" << before << " new=" << after
            << " removed_frame_bytes=" << bgra.size() << '\n';
}
int main() {
  try { regression(); benchmark(960,266); benchmark(936,280); benchmark(568,1210);
    std::cout << "DISPLAY_PIXEL_CONVERSION_OK\n"; return 0; }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

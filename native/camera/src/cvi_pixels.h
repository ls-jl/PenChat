#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace camera { namespace cvi {
// Convert four pixels together: rotation is resolved once and chroma reuses
// those same source pixels instead of reading/rotating the image a second time.
inline void bgraToNv12(const std::uint8_t *source, int width, int height,
    int rotation, std::uint8_t *target, int panelWidth, int panelHeight,
    std::size_t stride, std::size_t yBytes, std::size_t uvBytes) {
  std::memset(target, 16, yBytes);
  std::memset(target + yBytes, 128, uvBytes);
  std::ptrdiff_t origin = 0, across = 4, down = std::ptrdiff_t(width) * 4;
  if(rotation == 270) {origin = (width - 1) * 4; across = down; down = -4;}
  else if(rotation == 90) {origin = std::ptrdiff_t(height - 1) * width * 4; across = -down; down = 4;}
  else if(rotation == 180) {origin = (std::ptrdiff_t(width) * height - 1) * 4; across = -4; down = -down;}
  auto byte = [](int v) { return std::uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); };
  for(int y = 0; y < panelHeight; y += 2) {
    auto top = target + std::size_t(y) * stride;
    auto bottom = top + stride;
    auto uv = target + yBytes + std::size_t(y / 2) * stride;
    const auto row = source + origin + y * down;
    for(int x = 0; x < panelWidth; x += 2) {
      const auto a = row + x * across;
      const std::uint8_t *pixels[4] = {a, a + across, a + down, a + down + across};
      int red = 0, green = 0, blue = 0;
      std::uint8_t values[4];
      for(int n = 0; n < 4; ++n) {
        const int b = pixels[n][0], g = pixels[n][1], r = pixels[n][2];
        red += r; green += g; blue += b;
        values[n] = byte(((66*r + 129*g + 25*b + 128) >> 8) + 16);
      }
      top[x] = values[0]; top[x + 1] = values[1];
      bottom[x] = values[2]; bottom[x + 1] = values[3];
      red = (red + 2) / 4; green = (green + 2) / 4; blue = (blue + 2) / 4;
      uv[x] = byte(((-38*red - 74*green + 112*blue + 128) >> 8) + 128);
      uv[x + 1] = byte(((112*red - 94*green - 18*blue + 128) >> 8) + 128);
    }
  }
}
}}

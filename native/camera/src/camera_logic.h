#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace camera {
struct CropRect { int left, top, width, height; bool applied; };
CropRect aspectCrop(int boundsW, int boundsH, int outW, int outH);
CropRect mapPanelRect(int x, int y, int w, int h, int panelW, int panelH,
    int crtcW, int crtcH, int rotation);
CropRect fitPhysicalViewport(const CropRect& hole, int width, int height, int rotation,
    bool cover, double zoom = 1, double centerX = .5, double centerY = .5);
// Copies complete four-byte pixels without interpreting channel order.
bool resampleRgbaViewport(const uint8_t* src, int width, int height, uint8_t* dst,
    int dstW, int dstH, int imageX, int imageY, int imageW, int imageH, int rotation);
std::string formatUniqueMediaName(const char* prefix, const char* ext, int64_t sec, int ms, int counter);
// Red/blue swap: supports either RGBA->BGRA or BGRA->RGBA, including in-place.
void rgbaToBgra(const uint8_t* src, uint8_t* dst, size_t pixels);
} // namespace camera

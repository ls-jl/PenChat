#pragma once
#include <algorithm>
namespace penchat {
// Geometry is measured from the loaded Falcon logical screen, never a device
// resolution fallback. Sensor/photo dimensions are independent of these regions.
struct AppLayout {
  int width, height;
  bool portrait() const { return height > width; }
  int gutter() const { return portrait() ? 0 : 86; }
  int bodyWidth() const { return width - gutter() - 4; }
  int contentWidth() const { return width - (portrait() ? 32 : 118); }
  int composerX() const { return portrait() ? 16 : 106; }
  int composerWidth() const { return width - composerX() - (portrait() ? 16 : 24); }
  int modalX() const { return portrait() ? 12 : 84; }
  int modalWidth() const { return width - (portrait() ? 24 : 92); }
};
} // namespace penchat

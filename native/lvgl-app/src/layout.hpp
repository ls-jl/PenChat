#pragma once
#include <algorithm>
namespace penchat {
// Geometry is measured from the loaded Falcon logical screen, never a device
// resolution fallback. Sensor/photo dimensions are independent of these regions.
struct AppLayout {
  int width, height;
  bool portrait() const { return height > width; }
  // Portrait panels have strongly rounded physical corners. Keep interactive
  // content inset; backgrounds/images can still extend to the full canvas.
  int safeSide() const { return portrait() ? std::max(24, std::min(32, width / 24)) : 0; }
  int safeTop() const { return safeSide(); }
  int safeBottom() const { return portrait() ? safeSide() + 4 : 10; }
  int composerY() const { return height - 48 - safeBottom(); }
  int gutter() const { return portrait() ? 0 : 86; }
  int bodyWidth() const { return width - gutter() - 4; }
  int contentWidth() const { return width - (portrait() ? 32 : 118); }
  int composerX() const { return portrait() ? safeSide() : 106; }
  int composerWidth() const { return width - composerX() - (portrait() ? safeSide() : 24); }
  int modalX() const { return portrait() ? 12 : 84; }
  int modalWidth() const { return width - (portrait() ? 24 : 92); }
};
} // namespace penchat

#pragma once
#include <algorithm>
#include <cmath>

namespace penchat {
// Keyboard geometry is in Falcon logical coordinates. Fixed values below are
// padding/control sizes, never a fallback display resolution.
struct EditorLayout {
  int width, height, split, textX, textWidth, textY = 78, textHeight;
  int actionsY, bottomY, rowHeight, menuY;
  double keyWidth;
  EditorLayout(int w, int h) : width(w), height(h) {
    split = w - std::max(352, w * 2 / 5 - 8);
    textX = split + 8;
    textWidth = w - textX - 8;
    actionsY = h - 52;
    textHeight = actionsY - 6 - textY;
    bottomY = h - 50;
    rowHeight = (bottomY - 24) / 3;
    keyWidth = (split - 44) / 10.0;
    menuY = textY + std::min(50, std::max(0, textHeight - 52));
  }
  int px(double value) const { return int(std::lround(value)); }
  int textBottom() const { return textY + textHeight; }
};
} // namespace penchat

#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace penchat {
// Keyboard geometry is in Falcon logical coordinates. Fixed values below are
// padding/control sizes, never a fallback display resolution.
struct EditorLayout {
  int width, height, split, textX, textWidth, textY = 78, textHeight;
  int actionsY, bottomY, rowHeight, menuY;
  double keyWidth;
  EditorLayout(int w, int h, bool selectionOnly = false) : width(w), height(h) {
    split = w - std::max(352, w * 2 / 5 - 8);
    textX = split + 8;
    textWidth = w - textX - 8;
    actionsY = h - 52;
    textHeight = actionsY - 6 - textY;
    bottomY = h - 50;
    rowHeight = (bottomY - 24) / 3;
    keyWidth = (split - 44) / 10.0;
    if (selectionOnly) { split=86;textX=96;textWidth=w-106;textY=56;textHeight=h-66; }
    menuY = textY + std::min(50, std::max(0, textHeight - 52));
  }
  struct Popup {
    int x, y, width, height;
    bool contains(int px,int py) const {
      return px>=x&&px<x+width&&py>=y&&py<y+height;
    }
  };
  Popup undoPopup(int x,int y) const {
    return {std::max(textX,std::min(width-104,x-48)),
            std::max(4,std::min(actionsY-58,y-60)),96,52};
  }
  struct Action { const char *name; int x, width; };
  std::array<Action,5> actions() const {
    const int cancelWidth=textWidth-64-48-104-44-16;
    return {{{"cancel",textX,cancelWidth},
             {"official",textX+cancelWidth+4,64},
             {"ocr-mode",width-212,48},
             {"done",width-160,104},
             {"clipboard",width-52,44}}};
  }
  int px(double value) const { return int(std::lround(value)); }
  int textBottom() const { return textY + textHeight; }
};
} // namespace penchat

#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace penchat {
// Keyboard geometry is in Falcon logical coordinates. Fixed values below are
// padding/control sizes, never a fallback display resolution.
struct EditorLayout {
  int width, height, split, textX, textWidth, textY = 78, textHeight;
  int actionsY, bottomY, rowHeight;
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
  }
  struct Popup {
    int x, y, width, height;
    bool contains(int px,int py) const {
      return px>=x&&px<x+width&&py>=y&&py<y+height;
    }
  };
  Popup floatingPopup(int x,int y,int w,int h) const {
    return {std::max(4,std::min(width-w-4,x)),
            std::max(4,std::min(height-h-4,y)),w,h};
  }
  Popup undoPopup(int x,int y,bool canPaste=false) const {
    const int w=canPaste?188:96;
    return floatingPopup(x-w/2,y-60,w,52);
  }
  Popup selectionPopup(int x,int y,bool readOnly=false) const {
    const int w=(readOnly?2:4)*92+4;
    return floatingPopup(x-w/2,y-60,w,52);
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

#pragma once
#include "layout.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace penchat {
// Keyboard geometry is in Falcon logical coordinates. Fixed values below are
// padding/control sizes, never a fallback display resolution.
struct EditorLayout {
  int width, height, split, textX, textWidth, textY = 78, textHeight;
  int actionsY, bottomY, rowHeight, keyTop=6, bottomHeight=44, candidateY=6, compositionY=54;
  bool portrait=false, docked=false;
  int panelTop=0, textAreaWidth=0, keyboardLift=0;
  int keyInset=0, keySpan=0;
  double keyWidth;
  EditorLayout(int w, int h, bool selectionOnly = false, bool chatComposer = false) : width(w), height(h) {
    split = w - std::max(352, w * 2 / 5 - 8);
    textX = split + 8;
    textWidth = w - textX - 8;
    actionsY = h - 52;
    textHeight = actionsY - 6 - textY;
    bottomY = h - 50;
    rowHeight = (bottomY - 24) / 3;
    keyWidth = (split - 44) / 10.0;
    portrait=h>w;keySpan=split;
    if(portrait) {
      const AppLayout safe{w,h};
      split=w;keyInset=safe.safeSide();keySpan=w-2*keyInset;
      textX=keyInset;textWidth=keySpan;textY=safe.safeTop();
      const int baseRow=std::max(52,std::min(72,int(w*.12)));
      const int baseBottom=std::max(44,std::min(60,int(w*.10)));
      // Lift by one former bottom row (rounded to 10px), then grow the entire
      // four-row key region by 50% upwards. Gaps stay 6px; glyphs do not stretch.
      keyboardLift=int(std::lround(baseBottom/10.))*10;
      const int keyHeight=int(std::lround((3*baseRow+18+baseBottom)*1.5));
      bottomHeight=int(std::lround(baseBottom*1.5));
      rowHeight=int(std::lround((keyHeight-bottomHeight-18)/3.));
      bottomY=h-safe.safeBottom()-keyboardLift-bottomHeight;
      keyTop=bottomY-3*(rowHeight+6);
      actionsY=keyTop-52;candidateY=actionsY-50;compositionY=candidateY-26;
      textHeight=compositionY-textY-10;keyWidth=(keySpan-44)/10.;
    }
    docked=portrait&&!selectionOnly;
    if(docked) {
      textHeight=chatComposer?88:112;
      textY=compositionY-textHeight-10;
      panelTop=textY-(chatComposer?8:32);
    }
    if (selectionOnly) { split=86;textX=96;textWidth=w-106;textY=56;textHeight=h-66;if(portrait){const AppLayout safe{w,h};split=w;textX=keyInset;textWidth=keySpan;textY=56+safe.safeTop();textHeight=h-textY-safe.safeBottom();} }
    textAreaWidth=textWidth-(docked&&chatComposer?56:0);
  }
  int overlayY() const { return docked?AppLayout{width,height}.safeTop():textY; }
  int overlayHeight() const { return docked?compositionY-overlayY()-10:textHeight; }
  struct Popup {
    int x, y, width, height;
    bool contains(int px,int py) const {
      return px>=x&&px<x+width&&py>=y&&py<y+height;
    }
  };
  Popup keyTouchBounds(const Popup &cap, bool leftEdge = false,
                       bool rightEdge = false) const {
    const int left = leftEdge ? keyInset : std::max(keyInset, cap.x - 4);
    const int right = rightEdge ? keyInset+keySpan : std::min(keyInset+keySpan, cap.x + cap.width + 4);
    const int top = std::max(0, cap.y - 3);
    const int bottom = std::min(height, cap.y + cap.height + 3);
    return {left, top, right - left, bottom - top};
  }
  Popup floatingPopup(int x,int y,int w,int h) const {
    return {std::max(4,std::min(width-w-4,x)),
            std::max(4,std::min(height-h-4,y)),w,h};
  }
  Popup undoPopup(int x,int y,bool canPaste=false) const {
    const int w=canPaste?188:96;
    return floatingPopup(x-w/2,y-60-(portrait?52:0),w,52);
  }
  Popup selectionPopup(int x,int y,bool readOnly=false) const {
    const int w=(readOnly?2:4)*92+4;
    return floatingPopup(x-w/2,y-60-(portrait?52:0),w,52);
  }
  struct Action { const char *name; int x, width; };
  std::array<Action,5> actions() const {
    if(portrait){const int gap=std::max(4,(textWidth-456)/4);int x=textX;
      std::array<Action,5> a={{{"cancel",0,84},{"official",0,84},{"ocr-mode",0,112},{"done",0,128},{"clipboard",0,48}}};
      for(auto &v:a){v.x=x;x+=v.width+gap;}return a;
    }
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

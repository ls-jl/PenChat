#pragma once
#include "layout.hpp"
#include <algorithm>
#include <vector>
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
      split=w;keyInset=0;keySpan=w;
      textX=safe.safeSide();textWidth=w-2*textX;textY=safe.safeTop();
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
      actionsY=keyTop-52;candidateY=actionsY;compositionY=candidateY-26;
      textHeight=compositionY-textY-10;keyWidth=(keySpan-44)/10.;
    }
    docked=portrait&&!selectionOnly;
    if(docked) {
      // The keyboard has no dedicated editor. Text remains in the focused
      // page control; only composition/candidates/actions live above the keys.
      panelTop=compositionY-4;
      textHeight=44;textY=panelTop-52;
    }
    if (selectionOnly) { split=86;textX=96;textWidth=w-106;textY=56;textHeight=h-66;if(portrait){const AppLayout safe{w,h};split=w;textX=safe.safeSide();textWidth=w-2*textX;textY=56+safe.safeTop();textHeight=h-textY-safe.safeBottom();} }
    textAreaWidth=textWidth;
    (void)chatComposer;
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
  Popup magnifierPopup(int cursorX,int cursorY,int editorLeft=0,int editorRight=0) const {
    const AppLayout safe{width,height};
    const int side=std::min(std::max(4,safe.safeSide()),std::max(0,(width-1)/2));
    const int top=std::min(std::max(4,safe.safeTop()),std::max(0,(height-1)/2));
    const int bottom=std::min(std::max(4,safe.safeBottom()),std::max(0,height-top-1));
    // Visibility wins over preferred size, then the familiar position above
    // the caret. These are logical safe bounds, never raw touch coordinates.
    // Only the floating bubble is constrained by the visible editor sides.
    // Never feed this clamped position back into text/caret hit testing.
    const int left=editorRight>editorLeft?std::max(side,std::min(width-side-1,editorLeft)):side;
    const int right=editorRight>editorLeft?std::max(left+1,std::min(width-side,editorRight)):width-side;
    const int w=std::min(180,right-left),h=std::min(58,height-top-bottom);
    const int x=std::max(left,std::min(right-w,cursorX-w/2));
    const int y=std::max(top,std::min(height-bottom-h,cursorY-96-(portrait?58:0)));
    return {x,y,w,h};
  }
  Popup undoPopup(int x,int y,bool canPaste=false) const {
    const int w=canPaste?188:96;
    return floatingPopup(x-w/2,y-60-(portrait?52:0),w,52);
  }
  Popup selectionPopup(int x,int y,bool readOnly=false) const {
    const int w=(readOnly?2:4)*92+4;
    return floatingPopup(x-w/2,y-60-(portrait?52:0),w,52);
  }
  int utilityY() const { return bottomY+bottomHeight+6; }
  Popup globeKey() const {
    const int commaCenter=8+px(keyWidth+14)+px(keyWidth+10)/2;
    const int centerY=px((bottomY+bottomHeight-1.+height-1)/2.);
    return {commaCenter-14-24,centerY-3-24,48,48};
  }
  struct Action { const char *name; int x, width, y; };
  std::vector<Action> actions(bool chat=false) const {
    if(portrait) {
      std::vector<Action> a;
      if(!chat)a.push_back({"cancel",0,84,actionsY});
      a.push_back({"official",0,84,actionsY});a.push_back({"ocr-mode",0,112,actionsY});
      a.push_back({"clipboard",0,48,actionsY});
      int total=0;for(const auto &v:a)total+=v.width;
      const int gap=(textWidth-total)/int(a.size()-1);int x=textX;
      for(auto &v:a){v.x=x;x+=v.width+gap;}
      a.push_back({"done",width-textX-64,64,utilityY()});
      return a;
    }
    const int cancelWidth=textWidth-64-48-104-44-16;
    return {{"cancel",textX,cancelWidth,actionsY},
            {"official",textX+cancelWidth+4,64,actionsY},
            {"ocr-mode",width-212,48,actionsY},
            {"done",width-160,104,actionsY},
            {"clipboard",width-52,44,actionsY}};
  }
  int px(double value) const { return int(std::lround(value)); }
  int textBottom() const { return textY + textHeight; }
};
} // namespace penchat

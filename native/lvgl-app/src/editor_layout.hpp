#pragma once
#include "layout.hpp"
#include <algorithm>
#include <vector>
#include <cmath>

namespace penchat {
// Keyboard geometry is in Falcon logical coordinates. Fixed values below are
// padding/control sizes, never a fallback display resolution.
struct EditorLayout {
  int nativeWidth, nativeHeight;
  bool scaled=false, swapped=false;
  int width, height, split, textX, textWidth, textY = 56, textHeight;
  int actionsY, bottomY, rowHeight, keyTop=6, bottomHeight=44, candidateY=6, compositionY=6;
  bool portrait=false, docked=false, compact=false, compactFull=false;
  int panelTop=0, textAreaWidth=0, keyboardLift=0;
  int keyInset=0, keySpan=0;
  int keyGap=4, rowGap=6, keyMargin=4;
  double keyWidth;
  EditorLayout(int w, int h, bool selectionOnly = false, bool chatComposer = false,
               bool swapWideSides = false, bool compactExpanded = false,
               bool inlineReveal = false) : nativeWidth(w),nativeHeight(h),width(w),height(h) {
    compact=classifyLayout(w,h)==LayoutClass::CompactLandscape&&!selectionOnly;
    compactFull=compact&&!compactExpanded;
    swapped=swapWideSides&&!selectionOnly&&h<=w&&!compactFull;
    scaled=classifyLayout(w,h)==LayoutClass::CompactLandscape&&!compactFull;
    if(scaled) {
      w=width=960;h=height=266;
      keyGap=keyMargin=std::max(1,int(std::lround(4.*width/nativeWidth)));
      rowGap=std::max(1,int(std::lround(6.*height/nativeHeight)));
    }
    split = w - std::max(352, w * 2 / 5 - 8);
    textX = split + 8;
    textWidth = w - textX - 8;
    actionsY = h - 52;
    textHeight = actionsY - 6 - textY;
    bottomY = h - 50;
    rowHeight = (bottomY-keyTop-3*rowGap)/3;
    keyWidth = (split-2*keyMargin-9*keyGap)/10.0;
    portrait=h>w;keySpan=split;
    // Translate whole regions without reflecting their local coordinates. Key
    // order, text direction and hit bounds remain identical on either side.
    if(swapped) {keyInset=w-split;textX=8;}
    if(compactFull) {
      split=keySpan=w;keyInset=0;
      textX=100;textWidth=std::max(1,w-textX-52);
      textY=candidateY=compositionY=actionsY=0;textHeight=44;
      keyTop=48;bottomY=h-48;bottomHeight=44;
      rowHeight=std::max(1,(bottomY-keyTop-3*rowGap)/3);
      keyWidth=(keySpan-2*keyMargin-9*keyGap)/10.;
    }
    if(portrait) {
      const AppLayout safe{w,h};
      split=w;keyInset=0;keySpan=w;
      textX=safe.safeSide();textWidth=w-2*textX;textY=safe.safeTop();
      const int baseRow=std::max(52,std::min(72,int(w*.12)));
      const int baseBottom=std::max(44,std::min(60,int(w*.10)));
      // Lift by one former bottom row (rounded to 10px), then grow the entire
      // four-row key region by 50% upwards. Gaps stay 6px; glyphs do not stretch.
      keyboardLift=int(std::lround(baseBottom/10.))*10;
      const int keyHeight=int(std::lround((3*baseRow+3*rowGap+baseBottom)*1.5));
      bottomHeight=int(std::lround(baseBottom*1.5));
      rowHeight=int(std::lround((keyHeight-bottomHeight-3*rowGap)/3.));
      bottomY=h-safe.safeBottom()-keyboardLift-bottomHeight;
      keyTop=bottomY-3*(rowHeight+rowGap);
      actionsY=keyTop-52;candidateY=compositionY=actionsY;
      textHeight=compositionY-textY-10;keyWidth=(keySpan-2*keyMargin-9*keyGap)/10.;
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
    if(compactFull&&inlineReveal)textAreaWidth=std::max(1,textWidth-48);
    (void)chatComposer;
  }
  int fromNativeX(int x) const {return scaled?int(std::lround(double(x)*width/nativeWidth)):x;}
  int fromNativeY(int y) const {return scaled?int(std::lround(double(y)*height/nativeHeight)):y;}
  int scaleX() const {return scaled?int(std::lround(256.*nativeWidth/width)):256;}
  int scaleY() const {return scaled?int(std::lround(256.*nativeHeight/height)):256;}
  int editorPanelX() const { return portrait||compactFull?0:swapped?0:split; }
  int editorPanelWidth() const { return portrait||compactFull?width:width-split; }
  int textRight() const { return textX+textWidth; }
  int shiftWidth() const { return px(keyWidth+keyGap/2.); }
  double thirdRowX() const { return keyMargin+shiftWidth()+keyGap; }
  int deleteX() const { return px(thirdRowX()+7*(keyWidth+keyGap)); }
  int keysRight() const { return compactFull?width-68-keyGap:keySpan-keyMargin; }
  int overlayY() const { return compactFull?keyTop:docked?AppLayout{width,height}.safeTop():textY; }
  int overlayHeight() const { return compactFull?height-keyTop-4:docked?compositionY-overlayY()-10:textHeight; }
  int compactTouchWidth() const { return compact&&!compactFull?int(std::ceil(44.*256/scaleX())):44; }
  int controlHeight() const { return compact&&!compactFull?int(std::ceil(44.*256/scaleY())):44; }
  struct Popup {
    int x, y, width, height;
    bool contains(int px,int py) const {
      return px>=x&&px<x+width&&py>=y&&py<y+height;
    }
  };
  Popup compositionRow() const {
    return {textX,candidateY,compactFull?textWidth:
        std::max(1,textWidth-(compact?2*compactTouchWidth()+8:compactTouchWidth()+4)),44};
  }
  Popup compositionExpand() const {
    const auto row=compositionRow();
    return {row.x+row.width+4,row.y,compactTouchWidth(),44};
  }
  int compositionPreeditWidth(int textPixels) const {
    const int available=compositionRow().width;
    return std::max(12,std::min(std::min(144,std::max(12,available/3)),textPixels+8));
  }
  Popup compositionCandidates(bool expanded,int preeditWidth) const {
    const auto row=compositionRow();
    if(!expanded)return {row.x+preeditWidth+4,row.y,std::max(1,row.width-preeditWidth-4),44};
    if(compactFull)return {4,keyTop,width-8,height-keyTop-4};
    if(portrait)return {textX,keyTop,textWidth,bottomY+bottomHeight-keyTop};
    return {textX,textY,textWidth,textHeight};
  }
  Popup keyTouchBounds(const Popup &cap, bool leftEdge = false,
                       bool rightEdge = false) const {
    const int left = leftEdge ? keyInset : std::max(keyInset, cap.x - 4);
    const int right = rightEdge ? keyInset+keySpan : std::min(keyInset+keySpan, cap.x + cap.width + 4);
    const int top = std::max(0, cap.y - 3);
    const int bottom = std::min(height, cap.y + cap.height + 3);
    return {left, top, right - left, bottom - top};
  }
  Popup floatingPopup(int x,int y,int w,int h) const {
    const AppLayout safe{width,height};const int side=std::max(4,safe.safeSide()),top=std::max(4,safe.safeTop()),bottom=std::max(4,safe.safeBottom());
    w=std::min(w,width-2*side);h=std::min(h,height-top-bottom);
    return {std::max(side,std::min(width-w-side,x)),
            std::max(top,std::min(height-h-bottom,y)),w,h};
  }
  Popup magnifierPopup(int cursorX,int cursorY,int editorLeft=0,int editorRight=0) const {
    const AppLayout safe{width,height};
    const int side=std::min(std::max(4,safe.safeSide()),std::max(0,(width-1)/2));
    const int top=std::min(std::max(4,safe.safeTop()),std::max(0,(height-1)/2));
    const int bottom=std::min(std::max(4,safe.safeBottom()),std::max(0,height-top-1));
    // Visibility wins over preferred size, then the familiar position above
    // the caret. These are logical safe bounds, never raw touch coordinates.
    // Floating feedback may leave a narrow field; only the screen safe area
    // constrains it. Never feed its position back into caret hit testing.
    (void)editorLeft;(void)editorRight;
    const int left=side,right=width-side;
    const int w=std::min(180,right-left),h=std::min(58,height-top-bottom);
    const int x=std::max(left,std::min(right-w,cursorX-w/2));
    const int y=std::max(top,std::min(height-bottom-h,cursorY-96-(portrait?58:0)));
    return {x,y,w,h};
  }
  int selectionHandleWidth() const {return scaled?int(std::ceil(44.*256/scaleX())):44;}
  int selectionHandleHeight() const {return scaled?int(std::ceil(44.*256/scaleY())):44;}
  int selectionButtonWidth() const {return std::max(88,scaled?int(std::ceil(44.*256/scaleX())):44);}
  int selectionButtonHeight() const {return std::max(44,scaled?int(std::ceil(44.*256/scaleY())):44);}
  Popup globeKey() const {
    const int commaCenter=8+px(keyWidth+14)+px(keyWidth+10)/2;
    const int centerY=px((bottomY+bottomHeight-1.+height-1)/2.);
    return {commaCenter-14-24,centerY-3-24,48,48};
  }
  struct Action { const char *name; int x, width, y; };
  std::vector<Action> actions(bool chat=false, bool singleLineOcr=false) const {
    if(compactFull)return {{"cancel",4,44,0},{singleLineOcr?"clipboard":"tools",52,44,0},{"done",width-68,64,bottomY}};
    if(compact) {
      const int small=compactTouchWidth();
      if(singleLineOcr)return {{"cancel",textX,textWidth-small-112,actionsY},
        {"done",textRight()-small-108,104,actionsY},{"clipboard",textRight()-small,small,actionsY}};
      return {{"cancel",textX,textWidth-2*small-116,actionsY},
        {"ocr-mode",textRight()-104-2*small-8,small,actionsY},
        {"done",textRight()-small-108,104,actionsY},{"clipboard",textRight()-small,small,actionsY}};
    }
    if(portrait) {
      std::vector<Action> a;
      if(!chat)a.push_back({"cancel",0,84,actionsY});
      if(!singleLineOcr)a.push_back({"ocr-mode",0,112,actionsY});
      a.push_back({"clipboard",0,48,actionsY});
      int total=0;for(const auto &v:a)total+=v.width;
      const int gap=a.size()>1?(textWidth-total)/int(a.size()-1):0;int x=textX;
      for(auto &v:a){v.x=x;x+=v.width+gap;}
      const auto globe=globeKey();
      // Mirror pixel centres, including even-sized hit areas, around the
      // logical screen midpoint. The globe keeps its existing position.
      a.push_back({"done",width-globe.x-(globe.width+64)/2,64,
                   globe.y+(globe.height-44)/2});
      return a;
    }
    const int cancelWidth=textWidth-48-104-44-12;
    if(singleLineOcr)return {{"cancel",textX,textWidth-104-44-8,actionsY},
      {"done",textRight()-152,104,actionsY},{"clipboard",textRight()-44,44,actionsY}};
    return {{"cancel",textX,cancelWidth,actionsY},
            {"ocr-mode",textRight()-204,48,actionsY},
            {"done",textRight()-152,104,actionsY},
            {"clipboard",textRight()-44,44,actionsY}};
  }
  int px(double value) const { return int(std::lround(value)); }
  int textBottom() const { return textY + textHeight; }
};
} // namespace penchat

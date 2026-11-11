#pragma once
#include "layout.hpp"
#include "keyboard_design.hpp"
#include "keyboard_keys.hpp"
#include <algorithm>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace penchat {
// Keyboard geometry is in Falcon logical coordinates. Fixed values below are
// padding/control sizes, never a fallback display resolution.
struct EditorLayout {
  // Own only the resolved geometry needed after construction, never a borrowed
  // configuration or the complete design/style tables.
  KeyboardDesign::Keys keyMetrics;
  KeyboardDesign::Composition compositionMetrics;
  KeyboardCandidateMetrics candidateMetrics;
  KeyboardDesign::Controls controlMetrics;
  KeyboardDesign::Compact compactMetrics;
  KeyboardDesign::Popups popupMetrics;
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
               bool inlineReveal = false, const KeyboardDesign &design = keyboardDesign()) :
      keyMetrics(design.keys),compositionMetrics(design.composition),candidateMetrics(design.candidates),
      controlMetrics(design.controls),compactMetrics(design.compact),popupMetrics(design.popups),
      nativeWidth(w),nativeHeight(h),width(w),height(h) {
    keyGap=design.keys.gap;rowGap=design.keys.rowGap;keyMargin=design.keys.margin;
    keyTop=design.wide.keyTop;bottomHeight=design.controls.height;
    candidateY=compositionY=design.wide.keyTop;
    textY=design.wide.textTop;
    compact=classifyLayout(w,h)==LayoutClass::CompactLandscape&&!selectionOnly;
    compactFull=compact&&!compactExpanded;
    swapped=swapWideSides&&!selectionOnly&&h<=w&&!compactFull;
    scaled=classifyLayout(w,h)==LayoutClass::CompactLandscape&&!compactFull;
    if(scaled) {
      w=width=design.compact.splitCanvasWidth;h=height=design.compact.splitCanvasHeight;
      keyGap=std::max(1,int(std::lround(double(design.keys.gap)*width/nativeWidth)));
      keyMargin=std::max(1,int(std::lround(double(design.keys.margin)*width/nativeWidth)));
      rowGap=std::max(1,int(std::lround(double(design.keys.rowGap)*height/nativeHeight)));
    }
    split = w - std::max(design.wide.editorMinimum, int(w * design.wide.editorFraction) - design.wide.inset);
    textX = split + design.wide.inset;
    textWidth = w - textX - design.wide.inset;
    actionsY = h - design.controls.height - design.wide.inset;
    textHeight = actionsY - design.wide.textActionGap - textY;
    bottomY = h - bottomHeight - design.wide.bottomInset;
    rowHeight = (bottomY-keyTop-3*rowGap)/3;
    keyWidth = (split-2*keyMargin-9*keyGap)/10.0;
    portrait=h>w;keySpan=split;
    // Translate whole regions without reflecting their local coordinates. Key
    // order, text direction and hit bounds remain identical on either side.
    if(swapped) {keyInset=w-split;textX=design.wide.inset;}
    if(compactFull) {
      split=keySpan=w;keyInset=0;
      textX=design.compact.textLeading;textWidth=std::max(1,w-textX-design.compact.textTrailing);
      textY=candidateY=compositionY=actionsY=0;textHeight=design.compact.headerHeight;
      keyTop=design.compact.headerHeight+design.compact.headerGap;
      bottomHeight=design.controls.height;bottomY=h-bottomHeight-design.compact.bottomInset;
      rowHeight=std::max(1,(bottomY-keyTop-3*rowGap)/3);
      keyWidth=(keySpan-2*keyMargin-9*keyGap)/10.;
    }
    if(portrait) {
      const AppLayout safe{w,h};
      split=w;keyInset=0;keySpan=w;
      textX=safe.safeSide();textWidth=w-2*textX;textY=safe.safeTop();
      const auto &p=design.portrait;
      const int baseRow=std::max(p.rowMin,std::min(p.rowMax,int(w*p.rowRatio)));
      const int baseBottom=std::max(p.bottomMin,std::min(p.bottomMax,int(w*p.bottomRatio)));
      // Lift by one former bottom row (rounded to 10px), then grow the entire
      // four-row key region by 50% upwards. Gaps stay 6px; glyphs do not stretch.
      keyboardLift=int(std::lround(double(baseBottom)/p.liftStep))*p.liftStep;
      const int keyHeight=int(std::lround((3*baseRow+3*rowGap+baseBottom)*p.keyHeightScale));
      bottomHeight=int(std::lround(baseBottom*p.keyHeightScale));
      rowHeight=int(std::lround((keyHeight-bottomHeight-3*rowGap)/3.));
      bottomY=h-safe.safeBottom()-keyboardLift-bottomHeight;
      keyTop=bottomY-3*(rowHeight+rowGap);
      actionsY=keyTop-design.controls.height-p.actionGap;candidateY=compositionY=actionsY;
      textHeight=compositionY-textY-p.overlayGap;keyWidth=(keySpan-2*keyMargin-9*keyGap)/10.;
    }
    docked=portrait&&!selectionOnly;
    if(docked) {
      // The keyboard has no dedicated editor. Text remains in the focused
      // page control; only composition/candidates/actions live above the keys.
      panelTop=compositionY-design.portrait.panelInset;
      textHeight=design.controls.height;textY=panelTop-textHeight-design.portrait.editorGap;
    }
    if (selectionOnly) { split=86;textX=96;textWidth=w-106;textY=56;textHeight=h-66;if(portrait){const AppLayout safe{w,h};split=w;textX=safe.safeSide();textWidth=w-2*textX;textY=56+safe.safeTop();textHeight=h-textY-safe.safeBottom();} }
    textAreaWidth=textWidth;
    if(compactFull&&inlineReveal)textAreaWidth=std::max(1,textWidth-design.controls.height-design.controls.gap);
    (void)chatComposer;
  }
  KeyboardView view() const {return portrait?KeyboardView::Portrait:compact?
      (compactFull?KeyboardView::Compact:KeyboardView::CompactExpanded):KeyboardView::Wide;}
  int fromNativeX(int x) const {return scaled?int(std::lround(double(x)*width/nativeWidth)):x;}
  int fromNativeY(int y) const {return scaled?int(std::lround(double(y)*height/nativeHeight)):y;}
  int scaleX() const {return scaled?int(std::lround(256.*nativeWidth/width)):256;}
  int scaleY() const {return scaled?int(std::lround(256.*nativeHeight/height)):256;}
  int editorPanelX() const { return portrait||compactFull?0:swapped?0:split; }
  int editorPanelWidth() const { return portrait||compactFull?width:width-split; }
  int textRight() const { return textX+textWidth; }
  int keysRight() const { return compactFull?width-compactMetrics.doneWidth-compactMetrics.bottomInset-keyGap:keySpan-keyMargin; }
  int overlayY() const { return compactFull?keyTop:docked?AppLayout{width,height}.safeTop():textY; }
  int overlayHeight() const { return compactFull?height-keyTop-4:docked?compositionY-overlayY()-10:textHeight; }
  int compactTouchWidth() const { return compact&&!compactFull?int(std::ceil(double(controlMetrics.height)*256/scaleX())):controlMetrics.height; }
  int controlHeight() const { return compact&&!compactFull?int(std::ceil(double(controlMetrics.height)*256/scaleY())):controlMetrics.height; }
  using Popup=KeyboardRect;
  std::vector<KeyPlacement> placeKeys(const KeyboardKeys &keys) const {
    std::vector<KeyPlacement> result;
    auto row=[&](const KeyRow &r,int y,int h,int left,int right,double equal) {
      if(r.keys.empty())return;
      if(right<=left||h<=0||equal<=0||keyGap<0)
        throw std::invalid_argument("Keyboard row has no usable space");
      double occupied=keyGap*(r.keys.size()-1);int flexible=0,equalCount=0;
      for(const auto &k:r.keys) {
        const double w=k.width.measure(equal,keyGap);
        if(!r.pixelAdvance&&k.width.kind==KeyWidthKind::Equal)++equalCount;
        else occupied+=r.pixelAdvance?px(w):w;
        flexible+=k.width.kind==KeyWidthKind::Remaining;
      }
      occupied+=equalCount*equal;
      const double extra=flexible?(right-left-occupied)/flexible:0;
      if(occupied>right-left+.01||(flexible&&extra<=0))
        throw std::invalid_argument("Keyboard row widths exceed available space");
      const double start=r.centered?(keySpan-occupied)/2.:left;
      double fixedAdvance=0;int equalAdvance=0;
      for(size_t i=0;i<r.keys.size();++i) {
        const auto &k=r.keys[i];
        const double x=start+fixedAdvance+equalAdvance*(equal+keyGap);
        double w=k.width.kind==KeyWidthKind::Remaining?extra:k.width.measure(equal,keyGap);
        // A trailing fill ends exactly at the boundary, including fractional
        // equal-key advances. Bottom rows use the original integer advances.
        if(k.width.kind==KeyWidthKind::Remaining&&i+1==r.keys.size())w=right-px(x);
        const Popup cap{keyInset+px(x),y,px(w),h};
        if(cap.width<=0)throw std::invalid_argument("Keyboard key width must be positive");
        result.push_back({k,cap,keyTouchBounds(cap,r.edgeHits&&i==0,r.edgeHits&&i+1==r.keys.size())});
        if(!r.pixelAdvance&&k.width.kind==KeyWidthKind::Equal)++equalAdvance;
        else fixedAdvance+=(r.pixelAdvance?px(w):w)+keyGap;
      }
    };
    const int gridX=keyMargin+keyMetrics.filterWidth+keyGap;
    const double cell=(keySpan-keyMargin-gridX-2*keyGap)/3.;
    for(int i=0;i<3;++i)row(keys.rows[i],keyTop+i*(rowHeight+rowGap),rowHeight,
      keys.nine?gridX:keyMargin,keySpan-keyMargin,keys.nine?cell:keyWidth);
    if(!keys.nine)std::stable_partition(result.begin(),result.end(),[](const KeyPlacement &p){
      return p.spec.action!="shift"&&p.spec.action!="symbols"&&p.spec.action!="delete";
    });
    row(keys.bottom,bottomY,bottomHeight,keyMargin,keysRight(),keyWidth);
    if(portrait) {
      auto spec=keyboardKey("language","",KeyWidth::fixed(keyMetrics.utilitySize),"globe-large");
      spec.variant=KeyboardVariant::UtilityIcon;
      const auto cap=globeKey();
      const int left=std::max(keyMargin,cap.x-keyMetrics.utilityGrow);
      const int right=std::min(width-keyMargin,cap.x+cap.width+keyMetrics.utilityGrow);
      const int top=std::max(bottomY+bottomHeight+keyMetrics.touchY,cap.y-keyMetrics.utilityGrow);
      const int bottom=std::min(height-keyMargin,cap.y+cap.height+keyMetrics.utilityGrow);
      result.push_back({spec,cap,{left,top,right-left,bottom-top}});
    }
    return result;
  }
  Popup compositionRow() const {
    const int gap=compositionMetrics.gap;
    return {textX,candidateY,compactFull?textWidth:portrait?
        width-compositionMetrics.portraitRight-compositionMetrics.portraitExpandWidth-gap-textX:
        std::max(1,textWidth-(compact?2*compactTouchWidth()+2*gap:compactTouchWidth()+gap)),candidateMetrics.height};
  }
  Popup compositionExpand() const {
    const auto row=compositionRow();
    return portrait?Popup{width-compositionMetrics.portraitRight-compositionMetrics.portraitExpandWidth,
        candidateY-(compositionMetrics.portraitExpandHeight-candidateMetrics.height)/2,
        compositionMetrics.portraitExpandWidth,compositionMetrics.portraitExpandHeight}:
        Popup{row.x+row.width+compositionMetrics.gap,row.y,compactTouchWidth(),candidateMetrics.height};
  }
  int compositionPreeditWidth(int textPixels) const {
    const int available=compositionRow().width;
    const auto &c=compositionMetrics;
    return std::max(c.minimum,std::min(std::min(c.maximum,std::max(c.minimum,available/c.widthDivisor)),textPixels+c.padding));
  }
  Popup compositionCandidates(bool expanded,int preeditWidth) const {
    const auto row=compositionRow();const int gap=compositionMetrics.gap;
    if(!expanded)return {row.x+preeditWidth+gap,row.y,std::max(1,row.width-preeditWidth-gap),candidateMetrics.height};
    if(compactFull)return {gap,keyTop,width-2*gap,height-keyTop-gap};
    if(portrait)return {0,keyTop+gap,width,bottomY+bottomHeight-keyTop-2*gap};
    return {textX,textY,textWidth,textHeight};
  }
  Popup candidateCover() const {
    if(portrait)return {0,keyTop,width,bottomY+bottomHeight-keyTop};
    return compositionCandidates(true,0);
  }
  Popup keyTouchBounds(const Popup &cap, bool leftEdge = false,
                       bool rightEdge = false) const {
    const int left = leftEdge ? keyInset : std::max(keyInset, cap.x - keyMetrics.touchX);
    const int right = rightEdge ? keyInset+keySpan : std::min(keyInset+keySpan, cap.x + cap.width + keyMetrics.touchX);
    const int top = std::max(0, cap.y - keyMetrics.touchY);
    const int bottom = std::min(height, cap.y + cap.height + keyMetrics.touchY);
    return {left, top, right - left, bottom - top};
  }
  Popup floatingPopup(int x,int y,int w,int h) const {
    const AppLayout safe{width,height};const int side=std::max(popupMetrics.safeMargin,safe.safeSide()),top=std::max(popupMetrics.safeMargin,safe.safeTop()),bottom=std::max(popupMetrics.safeMargin,safe.safeBottom());
    w=std::min(w,width-2*side);h=std::min(h,height-top-bottom);
    return {std::max(side,std::min(width-w-side,x)),
            std::max(top,std::min(height-h-bottom,y)),w,h};
  }
  Popup magnifierPopup(int cursorX,int cursorY,int editorLeft=0,int editorRight=0) const {
    const AppLayout safe{width,height};
    const int side=std::min(std::max(popupMetrics.safeMargin,safe.safeSide()),std::max(0,(width-1)/2));
    const int top=std::min(std::max(popupMetrics.safeMargin,safe.safeTop()),std::max(0,(height-1)/2));
    const int bottom=std::min(std::max(popupMetrics.safeMargin,safe.safeBottom()),std::max(0,height-top-1));
    // Visibility wins over preferred size, then the familiar position above
    // the caret. These are logical safe bounds, never raw touch coordinates.
    // Floating feedback may leave a narrow field; only the screen safe area
    // constrains it. Never feed its position back into caret hit testing.
    (void)editorLeft;(void)editorRight;
    const int left=side,right=width-side;
    const int w=std::min(popupMetrics.lensWidth,right-left),h=std::min(popupMetrics.lensHeight,height-top-bottom);
    const int x=std::max(left,std::min(right-w,cursorX-w/2));
    const int y=std::max(top,std::min(height-bottom-h,cursorY-popupMetrics.lensOffset-(portrait?popupMetrics.portraitLensOffset:0)));
    return {x,y,w,h};
  }
  int selectionHandleWidth() const {return scaled?int(std::ceil(double(controlMetrics.height)*256/scaleX())):controlMetrics.height;}
  int selectionHandleHeight() const {return scaled?int(std::ceil(double(controlMetrics.height)*256/scaleY())):controlMetrics.height;}
  int selectionButtonWidth() const {return std::max(88,scaled?int(std::ceil(double(controlMetrics.height)*256/scaleX())):controlMetrics.height);}
  int selectionButtonHeight() const {return std::max(44,scaled?int(std::ceil(double(controlMetrics.height)*256/scaleY())):controlMetrics.height);}
  Popup globeKey() const {
    const int commaCenter=8+px(keyWidth+14)+px(keyWidth+10)/2;
    const int centerY=px((bottomY+bottomHeight-1.+height-1)/2.);
    return {commaCenter-keyMetrics.utilityShiftX-keyMetrics.utilitySize/2,
            centerY-keyMetrics.utilityShiftY-keyMetrics.utilitySize/2,keyMetrics.utilitySize,keyMetrics.utilitySize};
  }
  struct Action { const char *name; int x, width, y; };
  std::vector<Action> actions(bool chat=false, bool singleLineOcr=false) const {
    const auto &c=controlMetrics;const int gap=c.gap;
    if(compactFull)return {{"cancel",gap,c.height,0},{singleLineOcr?"clipboard":"tools",2*gap+c.height,c.height,0},
      {"done",width-compactMetrics.doneWidth-compactMetrics.bottomInset,compactMetrics.doneWidth,bottomY}};
    if(portrait) {
      std::vector<Action> a;
      if(!chat)a.push_back({"cancel",0,c.portraitCancelWidth,actionsY});
      if(!singleLineOcr)a.push_back({"ocr-mode",0,c.portraitOcrWidth,actionsY});
      a.push_back({"clipboard",0,c.portraitClipboardWidth,actionsY});
      int total=0;for(const auto &v:a)total+=v.width;
      const int spacing=a.size()>1?(textWidth-total)/int(a.size()-1):0;int x=textX;
      for(auto &v:a){v.x=x;x+=v.width+spacing;}
      const auto globe=globeKey();
      a.push_back({"done",width-globe.x-(globe.width+c.portraitDoneWidth)/2,c.portraitDoneWidth,
                   globe.y+(globe.height-c.height)/2});
      return a;
    }
    const int clip=compact?compactTouchWidth():c.clipboardWidth;
    const int ocr=compact?compactTouchWidth():c.ocrWidth;
    const int doneX=textRight()-clip-gap-c.doneWidth;
    std::vector<Action> a{{"cancel",textX,textWidth-clip-c.doneWidth-2*gap-(singleLineOcr?0:ocr+gap),actionsY}};
    if(!singleLineOcr)a.push_back({"ocr-mode",doneX-gap-ocr,ocr,actionsY});
    a.push_back({"done",doneX,c.doneWidth,actionsY});
    a.push_back({"clipboard",textRight()-clip,clip,actionsY});
    return a;
  }
  int px(double value) const { return int(std::lround(value)); }
  int textBottom() const { return textY + textHeight; }
};
} // namespace penchat

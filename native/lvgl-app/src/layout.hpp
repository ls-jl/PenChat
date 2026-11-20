#pragma once
#include "ui_metrics.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace penchat {
enum class LayoutClass { Portrait, WideLandscape, CompactLandscape };
inline LayoutClass classifyLayout(int width, int height) {
  if (height > width) return LayoutClass::Portrait;
  return int64_t(width) >= int64_t(height) * 3
      ? LayoutClass::WideLandscape : LayoutClass::CompactLandscape;
}

// Geometry is measured from the loaded Falcon logical screen, never a device
// resolution fallback. Sensor/photo dimensions are independent of these regions.
struct LayoutRect { int x,y,width,height; };
// Provisional Y07 optical mask. Geometry is always logical, and the explicit
// capability must never be inferred from the resolution or aspect ratio.
struct ScreenSafety {
  int width=0,height=0;
  bool rounded=false;
  bool enabled() const { return rounded && height>width && width>0; }
  int radius() const { return enabled()?int(std::ceil(std::min(width,height)*.20)):0; }
  int margin() const { return enabled()?int(std::ceil(std::min(width,height)*.015)):0; }
  int cornerInset() const { return enabled()?int(std::ceil(std::min(width,height)*.08)):0; }
  int sideInset(int y,int h) const {
    if(!enabled())return 0;
    const int r=radius(),m=margin();
    const int t=std::max(0,std::min(y,height-y-h));
    if(t>=r)return m;
    return int(std::ceil(r-std::sqrt(double(r)*r-double(r-t)*(r-t))+m));
  }
  LayoutRect fit(LayoutRect rect) const {
    if(!enabled())return rect;
    const int m=margin();
    rect.height=std::min(rect.height,std::max(1,height-2*m));
    rect.y=std::max(m,std::min(height-m-rect.height,rect.y));
    const int side=sideInset(rect.y,rect.height);
    rect.width=std::min(rect.width,std::max(1,width-2*side));
    rect.x=std::max(side,std::min(width-side-rect.width,rect.x));
    return rect;
  }
};
struct ScanWorkspace {
  // Reader-local content; use/cancel are fixed screen-canvas coordinates.
  LayoutRect caption, preview, warning, use, cancel;
  int captionFloor, previewBottom;
};
struct CameraWorkspace {
  LayoutRect preview, direction, shutter, turn;
};
struct CropWorkspace {
  LayoutRect viewport, cancel, reset, album, confirm;
};
struct GuideWorkspace {
  bool portrait; int split,right,measure,footer,demoHeight;
};
struct AppLayout {
  int width, height;
  bool rounded=false;
  UiMetrics metrics;
  AppLayout(int w, int h, bool round = false, const UiMetrics &ui = UiMetrics())
      : width(w), height(h), rounded(round), metrics(ui) {}
  int dp(int value) const { return metrics.dp(value); }
  ScreenSafety safety() const { return {width,height,rounded}; }
  LayoutRect edgeRect(LayoutRect rect) const {
    if(safety().enabled()&&metrics.factor()<1.) {
      const int w=std::max(UiMetrics::minimumTouchTarget(),rect.width),h=std::max(UiMetrics::minimumTouchTarget(),rect.height);
      rect.x-=(w-rect.width)/2;rect.y-=(h-rect.height)/2;rect.width=w;rect.height=h;
    }
    return safety().fit(rect);
  }
  int edgeSide(int y,int h) const { return std::max(safeSide(),safety().sideInset(y,h)); }
  int navigationX() const { return edgeSide(safeTop(),metrics.touchTarget(48)); }
  LayoutClass category() const { return classifyLayout(width,height); }
  bool portrait() const { return category() == LayoutClass::Portrait; }
  bool compact() const { return category() == LayoutClass::CompactLandscape; }
  int safeSide() const { return portrait() ? std::max(dp(24), std::min(dp(32), width / 24)) : 0; }
  int safeTop() const { return std::max(safeSide(),safety().cornerInset()); }
  int safeBottom() const { return portrait() ? std::max(safeSide()+dp(4),safety().cornerInset()) : dp(10); }
  int readerBottomSpace() const { return safety().enabled()?safeBottom()+2*safety().margin():dp(12); }
  int composerInset() const { const auto c=metrics.composer();return portrait()?c.portraitInset:c.landscapeInset; }
  int composerSendDiameter() const { const auto c=metrics.composer();return portrait()?c.portraitSend:c.control; }
  int composerHeight() const { return metrics.composer().control+2*composerInset(); }
  int composerEntryLeft() const { return portrait() ? composerInset()+metrics.composer().control+dp(12) : metrics.composer().control+dp(18); }
  int composerEntryRight(bool gauge) const { const int control=metrics.composer().control;return portrait() ? composerInset()+control+dp(12)+(gauge?control+dp(6):0) : control+(gauge?control+dp(18):dp(10)); }
  int composerY() const { return height - composerHeight() - safeBottom(); }
  int gutter() const { return portrait() ? 0 : dp(86); }
  int bodyWidth() const { return std::max(1,width - gutter() - dp(4)); }
  int chatReaderTop() const { return portrait() ? safeTop() + std::max(UiMetrics::minimumTouchTarget()+dp(8),dp(56)) : 0; }
  int chatMessageWidth() const { return std::max(1,portrait() ? bodyWidth() - dp(16) : width - dp(112)); }
  int contentWidth() const { return std::max(1,width - dp(portrait()?32:118)); }
  int composerX() const { return portrait() ? std::max(dp(16),safeSide()-dp(4)) : dp(106); }
  LayoutRect composerShortcut() const {
    if(portrait())return {dp(12),composerY(),std::max(dp(44),composerX()-dp(24)),composerHeight()};
    return {composerX()-dp(44),composerY()-dp(46),dp(44),dp(44)};
  }
  int composerWidth() const { return std::max(1,width - composerX() - (portrait() ? composerX() : dp(24))); }
  int modalX() const { return dp(portrait()?12:84); }
  int modalWidth() const { return std::max(1,width - dp(portrait()?24:92)); }
  ScanWorkspace scanWorkspace() const {
    const int control=metrics.touchTarget(),gap=dp(12);
    const int rowY=height-safeBottom()-control;
    const int side=std::max(dp(16),edgeSide(rowY,control));
    const int buttonWidth=std::max(1,std::min(dp(200),(width-2*side-gap)/2));
    const int left=(width-2*buttonWidth-gap)/2;
    const LayoutRect use{left,rowY,buttonWidth,control},cancel{left+buttonWidth+gap,rowY,buttonWidth,control};
    const int top=portrait()?safeTop():0;
    const int previewTop=portrait()?top+dp(48):dp(32),bottom=rowY-dp(12);
    const LayoutRect caption=portrait()?LayoutRect{navigationX()+dp(56),top+dp(8),width-navigationX()-dp(80),dp(24)}
      :LayoutRect{dp(10),0,bodyWidth()-dp(20),dp(24)};
    return {caption,{dp(10),previewTop,bodyWidth()-dp(20),std::max(1,bottom-previewTop)},
      {dp(10),top+dp(28),bodyWidth()-dp(20),dp(24)},use,cancel,portrait()?top+dp(40):dp(28),bottom};
  }
  CameraWorkspace cameraWorkspace(int w,int h,bool quarterTurn) const {
    if(portrait()||quarterTurn) {
      const int diameter=portrait()?dp(84):dp(72),rowHeight=dp(104);
      const int rowY=h-(portrait()?safeBottom():dp(12))-rowHeight;
      const int column=(w-diameter)/2;
      const int auxiliary=std::min(dp(128),column-dp(24)),left=(column-auxiliary)/2;
      return {{dp(8),dp(8),w-dp(16),rowY-dp(24)},{left,rowY+dp(16),auxiliary,dp(72)},
        {(w-diameter)/2,rowY+(rowHeight-diameter)/2,diameter,diameter},
        {w-left-auxiliary,rowY+dp(16),auxiliary,dp(72)}};
    }
    const int diameter=std::min(dp(72),std::max(dp(48),h-dp(144)));
    return {{dp(8),dp(8),w-dp(144),h-dp(16)},{w-dp(132),dp(12),dp(124),dp(48)},
      {w-dp(70)-diameter/2,(h-diameter)/2,diameter,diameter},
      {w-dp(132),h-dp(60),dp(124),dp(48)}};
  }
  CropWorkspace cropWorkspace(int w,int h) const {
    if(portrait()) {
      const int control=metrics.touchTarget(48),album=metrics.touchTarget();
      const int side=edgeSide(safeTop(),album),available=w-2*side;
      const int y=h-safeBottom()-dp(12)-control,albumY=y-album-dp(12);
      const int buttonWidth=(available-dp(24))/3;
      return {{side,safeTop(),available,albumY-safeTop()-dp(16)},
        {side,y,buttonWidth,control},{side+buttonWidth+dp(12),y,buttonWidth,control},
        {side,albumY,available,album},{w-side-buttonWidth,y,buttonWidth,control}};
    }
    const bool narrow=w<dp(440);const int y=h-dp(52),bw=narrow?(w-dp(32))/3:dp(72);
    return {{dp(8),dp(8),w-dp(16),h-(narrow?dp(120):dp(68))},{dp(8),y,bw,dp(44)},
      {narrow?dp(16)+bw:dp(88),y,bw,dp(44)},{narrow?dp(12):dp(168),narrow?h-dp(104):y,narrow?w-dp(24):dp(184),dp(44)},
      {w-dp(8)-bw,y,bw,dp(44)}};
  }
  GuideWorkspace guideWorkspace() const {
    const bool p=portrait();const int split=p?width-dp(4):width/2,right=p?dp(20):split+dp(20);
    const int side=safety().enabled()?navigationX():right;
    return {p,split,side,width-side-(p?side:dp(20)),height-metrics.touchTarget()-(safety().enabled()?safeBottom():dp(12)),p?std::min(dp(520),height*43/100):height-dp(80)};
  }
};
} // namespace penchat

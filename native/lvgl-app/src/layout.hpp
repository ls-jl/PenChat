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
  LayoutRect edgeRect(LayoutRect rect) const { return safety().fit(rect); }
  int edgeSide(int y,int h) const { return std::max(safeSide(),safety().sideInset(y,h)); }
  int navigationX() const { return edgeSide(safeTop(),dp(48)); }
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
  int composerEntryLeft() const { return portrait() ? composerInset()+dp(56) : dp(62); }
  int composerEntryRight(bool gauge) const { return portrait() ? composerInset()+dp(56)+(gauge?dp(50):0) : dp(gauge?106:54); }
  int composerY() const { return height - composerHeight() - safeBottom(); }
  int gutter() const { return portrait() ? 0 : dp(86); }
  int bodyWidth() const { return std::max(1,width - gutter() - dp(4)); }
  int chatReaderTop() const { return portrait() ? safeTop() + dp(56) : 0; }
  int chatMessageWidth() const { return std::max(1,portrait() ? bodyWidth() - dp(16) : width - dp(112)); }
  int contentWidth() const { return std::max(1,width - dp(portrait()?32:118)); }
  int composerX() const { return portrait() ? safeSide() : dp(106); }
  LayoutRect composerShortcut() const {
    if(portrait())return {dp(12),composerY(),std::max(dp(44),composerX()-dp(24)),composerHeight()};
    return {composerX()-dp(44),composerY()-dp(46),dp(44),dp(44)};
  }
  int composerWidth() const { return std::max(1,width - composerX() - (portrait() ? safeSide() : dp(24))); }
  int modalX() const { return dp(portrait()?12:84); }
  int modalWidth() const { return std::max(1,width - dp(portrait()?24:92)); }
  ScanWorkspace scanWorkspace() const {
    if(portrait()) {
      if(!safety().enabled())return {{dp(72),dp(16),width-dp(88),dp(24)},{dp(10),dp(56),width-dp(20),height-dp(186)},
        {dp(16),dp(28),width-dp(32),dp(24)},{dp(16),height-dp(112),width-dp(32),dp(44)},{dp(16),height-dp(58),width-dp(32),dp(44)},dp(48),height-dp(130)};
      const int top=safeTop(),cancelY=height-safeBottom()-dp(44),useY=cancelY-dp(52);
      return {{navigationX()+dp(56),top+dp(8),width-navigationX()-dp(80),dp(24)},
        {dp(10),top+dp(48),width-dp(20),useY-top-dp(64)},{dp(16),top+dp(28),width-dp(32),dp(24)},
        edgeRect({dp(16),useY,width-dp(32),dp(44)}),edgeRect({dp(16),cancelY,width-dp(32),dp(44)}),top+dp(40),useY-dp(16)};
    }
    return {{dp(10),0,width-dp(120),dp(24)},{dp(10),dp(32),width-dp(330),height-dp(88)},
      {dp(10),dp(28),width-dp(330),dp(24)},{width-dp(305),dp(32),dp(200),dp(44)},{width-dp(305),dp(86),dp(200),dp(44)},dp(28),height-dp(56)};
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
      const int side=edgeSide(safeTop(),dp(44)),available=w-2*side;
      const int y=h-safeBottom()-dp(12)-dp(48),albumY=y-dp(56);
      const int buttonWidth=(available-dp(24))/3;
      return {{side,safeTop(),available,albumY-safeTop()-dp(16)},
        {side,y,buttonWidth,dp(48)},{side+buttonWidth+dp(12),y,buttonWidth,dp(48)},
        {side,albumY,available,dp(44)},{w-side-buttonWidth,y,buttonWidth,dp(48)}};
    }
    const bool narrow=w<dp(440);const int y=h-dp(52),bw=narrow?(w-dp(32))/3:dp(72);
    return {{dp(8),dp(8),w-dp(16),h-(narrow?dp(120):dp(68))},{dp(8),y,bw,dp(44)},
      {narrow?dp(16)+bw:dp(88),y,bw,dp(44)},{narrow?dp(12):dp(168),narrow?h-dp(104):y,narrow?w-dp(24):dp(184),dp(44)},
      {w-dp(8)-bw,y,bw,dp(44)}};
  }
  GuideWorkspace guideWorkspace() const {
    const bool p=portrait();const int split=p?width-dp(4):width/2,right=p?dp(20):split+dp(20);
    const int side=safety().enabled()?navigationX():right;
    return {p,split,side,width-side-(p?side:dp(20)),height-(safety().enabled()?safeBottom()+dp(44):dp(56)),p?std::min(dp(520),height*43/100):height-dp(80)};
  }
};
} // namespace penchat

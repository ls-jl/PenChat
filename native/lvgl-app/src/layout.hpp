#pragma once
#include <algorithm>
#include <cstdint>
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
struct ScanWorkspace {
  LayoutRect caption, preview, warning, use, cancel;
  int captionFloor, previewBottom;
};
struct CameraWorkspace {
  LayoutRect preview, direction, shutter, turn;
};
struct GuideWorkspace {
  bool portrait; int split,right,measure,footer,demoHeight;
};
struct AppLayout {
  int width, height;
  LayoutClass category() const { return classifyLayout(width,height); }
  bool portrait() const { return category() == LayoutClass::Portrait; }
  bool compact() const { return category() == LayoutClass::CompactLandscape; }
  // Portrait panels have strongly rounded physical corners. Keep interactive
  // content inset; backgrounds/images can still extend to the full canvas.
  int safeSide() const { return portrait() ? std::max(24, std::min(32, width / 24)) : 0; }
  int safeTop() const { return safeSide(); }
  int safeBottom() const { return portrait() ? safeSide() + 4 : 10; }
  int composerInset() const { return portrait() ? 25 : 2; }
  int composerHeight() const { return 44+2*composerInset(); }
  int composerEntryLeft() const { return portrait() ? composerInset()+56 : 62; }
  int composerEntryRight(bool gauge) const { return portrait() ? composerInset()+56+(gauge?50:0) : gauge?106:54; }
  int composerY() const { return height - composerHeight() - safeBottom(); }
  int gutter() const { return portrait() ? 0 : 86; }
  int bodyWidth() const { return width - gutter() - 4; }
  int contentWidth() const { return width - (portrait() ? 32 : 118); }
  int composerX() const { return portrait() ? safeSide() : 106; }
  int composerWidth() const { return width - composerX() - (portrait() ? safeSide() : 24); }
  int modalX() const { return portrait() ? 12 : 84; }
  int modalWidth() const { return width - (portrait() ? 24 : 92); }
  ScanWorkspace scanWorkspace() const {
    if(portrait()) return {{72,16,width-88,24},{10,56,width-20,height-186},
      {16,28,width-32,24},{16,height-112,width-32,44},{16,height-60,width-32,44},48,height-130};
    return {{10,0,width-120,24},{10,32,width-330,height-88},
      {10,28,width-330,24},{width-305,32,200,44},{width-305,84,200,44},28,height-56};
  }
  CameraWorkspace cameraWorkspace(int w,int h,bool quarterTurn) const {
    if(portrait()||quarterTurn)return {{8,8,w-16,h-204},{12,h-190,w-24,44},
      {(w-72)/2,h-126,72,72},{12,h-48,w-24,44}};
    return {{8,8,w-140,h-16},{w-130,16,124,44},
      {w-104,(h-72)/2,72,72},{w-130,h-66,124,44}};
  }
  GuideWorkspace guideWorkspace() const {
    const bool p=portrait();const int split=p?width-4:width/2,right=p?20:split+20;
    return {p,split,right,width-right-20,height-56,p?std::min(520,height*43/100):height-80};
  }
};
} // namespace penchat

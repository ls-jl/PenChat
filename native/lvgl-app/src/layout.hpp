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
struct CropWorkspace {
  LayoutRect viewport, cancel, reset, album, confirm;
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
  LayoutRect composerShortcut() const {
    return {12, composerY(), std::max(44, composerX() - 24), composerHeight()};
  }
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
    if(portrait()||quarterTurn) {
      const int diameter=portrait()?84:72,rowHeight=104;
      const int rowY=h-(portrait()?safeBottom():12)-rowHeight;
      const int column=(w-diameter)/2;
      const int auxiliary=std::min(128,column-24),left=(column-auxiliary)/2;
      return {{8,8,w-16,rowY-24},{left,rowY+16,auxiliary,72},
        {(w-diameter)/2,rowY+(rowHeight-diameter)/2,diameter,diameter},
        {w-left-auxiliary,rowY+16,auxiliary,72}};
    }
    const int diameter=std::min(72,std::max(48,h-144));
    return {{8,8,w-144,h-16},{w-132,12,124,48},
      {w-70-diameter/2,(h-diameter)/2,diameter,diameter},
      {w-132,h-60,124,48}};
  }
  CropWorkspace cropWorkspace(int w,int h) const {
    if(portrait()) {
      const int side=safeSide(),available=w-2*side;
      const int y=h-safeBottom()-12-48,albumY=y-56;
      const int buttonWidth=(available-24)/3;
      return {{side,safeTop(),available,albumY-safeTop()-16},
        {side,y,buttonWidth,48},{side+buttonWidth+12,y,buttonWidth,48},
        {side,albumY,available,44},{w-side-buttonWidth,y,buttonWidth,48}};
    }
    const bool narrow=w<440;const int y=h-52,bw=narrow?(w-32)/3:72;
    return {{8,8,w-16,h-(narrow?120:68)},{8,y,bw,44},
      {narrow?16+bw:88,y,bw,44},{narrow?12:168,narrow?h-104:y,narrow?w-24:184,44},
      {w-8-bw,y,bw,44}};
  }
  GuideWorkspace guideWorkspace() const {
    const bool p=portrait();const int split=p?width-4:width/2,right=p?20:split+20;
    return {p,split,right,width-right-20,height-56,p?std::min(520,height*43/100):height-80};
  }
};
} // namespace penchat

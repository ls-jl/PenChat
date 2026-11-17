#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
namespace camera {
class DisplayUnavailable : public std::runtime_error {
public:
  explicit DisplayUnavailable(const std::string &message):std::runtime_error(message) {}
};
enum class CompositionMode { Unselected, Underlay, UpperOverlay, Shared };
inline const char *compositionName(CompositionMode mode) {
  return mode==CompositionMode::Underlay?"underlay":mode==CompositionMode::UpperOverlay?"upper_overlay":
         mode==CompositionMode::Shared?"shared":"unselected";
}
struct DisplayInfo {
  CompositionMode mode=CompositionMode::Shared;
  uint32_t primary=0,crtc=0,framebuffer=0,format=0,plane=0,nativeFormat=0;
  int queryResult=-1,queryError=0,attempts=0;
  bool atomic=false;
  std::array<uint64_t,3> nativeRange{{0,0,0}},hostRange{{0,0,0}};
  std::string stage,reason,completion;
};
class DisplayInitializationError : public std::runtime_error {
public:
  DisplayInfo info;
  DisplayInitializationError(const std::string &text,const DisplayInfo &value):std::runtime_error(text),info(value){}
};
struct DisplayGeometry {
  int width=0,height=0,rotation=0,xoffset=0,yoffset=0;
  int panelWidth=0,panelHeight=0;
};
// Only the display implementation knows DRM/CVI. All LVGL pages share this API.
class DisplaySurface {
public:
  virtual ~DisplaySurface() = default;
  // False means hardware is still using every output buffer. Keep the latest
  // CPU frame and retry outside LVGL rendering; do not overwrite an active DMA.
  virtual bool present(const std::uint8_t *,int,int)=0;
  virtual void hide()=0;
  // Logical top-only visibility for an in-place system editor. Zero is full.
  virtual bool supportsTopRegion() const { return false; }
  virtual void topRegion(int) {}
  // Poll asynchronous transport failures even when the UI has no dirty pixels.
  virtual void check() {}
  virtual DisplayInfo information() const { return {}; }
  // UI scheduling target, independent of physical panel mode. Direct planes
  // present immediately; asynchronous compositors use it to pace latest frames.
  virtual void setRefreshRate(int) {}
  // Shared-memory displays do not hold video/capture resources across scans.
  virtual bool sharedCaptureSafe() const {return false;}
  // A compositor may retain this lower layer while system windows own input.
  // Direct hardware planes must still hide when they lose window focus.
  virtual bool systemOverlaySafe() const {return false;}
  virtual std::array<uint64_t,4> stacking() {return {{0,0,0,0}};}
  // Some legacy video drivers invalidate old handles when another owner opens.
  virtual bool releaseOnHide() const {return false;}
};
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &);
}

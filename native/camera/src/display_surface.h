#pragma once
#include <cstdint>
#include <memory>
namespace camera {
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
  // Shared-memory displays do not hold video/capture resources across scans.
  virtual bool sharedCaptureSafe() const {return false;}
  // A compositor may retain this lower layer while system windows own input.
  // Direct hardware planes must still hide when they lose window focus.
  virtual bool systemOverlaySafe() const {return false;}
  // Some legacy video drivers invalidate old handles when another owner opens.
  virtual bool releaseOnHide() const {return false;}
};
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &);
}

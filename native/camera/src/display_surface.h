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
  // Poll asynchronous transport failures even when the UI has no dirty pixels.
  virtual void check() {}
  // Shared-memory displays do not hold video/capture resources across scans.
  virtual bool sharedCaptureSafe() const {return false;}
  // Some legacy video drivers invalidate old handles when another owner opens.
  virtual bool releaseOnHide() const {return false;}
};
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &);
}

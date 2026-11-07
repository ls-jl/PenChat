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
  virtual void present(const std::uint8_t *,int,int)=0;
  virtual void hide()=0;
};
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &);
}

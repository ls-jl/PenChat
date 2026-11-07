#include "display_surface.h"
#include "overlay.h"
#include <stdexcept>
namespace camera {
namespace {
class DrmDisplay final : public DisplaySurface {
  Overlay overlay_;
public:
  explicit DrmDisplay(const DisplayGeometry &g) {
    overlay_.setHole({0,0,g.width,g.height,g.width,g.height,g.rotation,0,0,g.xoffset,g.yoffset});
    if(!overlay_.open())throw std::runtime_error(overlay_.lastError());
  }
  bool present(const std::uint8_t *pixels,int w,int h) override {
    if(!overlay_.presentBgra(pixels,w,h))throw std::runtime_error(overlay_.lastError());
    return true;
  }
  void hide() override {overlay_.disable();}
};
}
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &g) {
  return std::unique_ptr<DisplaySurface>(new DrmDisplay(g));
}
}

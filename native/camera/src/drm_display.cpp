#include "display_surface.h"
#include "overlay.h"
#include <stdexcept>
namespace camera {
namespace {
class DrmDisplay final : public DisplaySurface {
  Overlay overlay_;
  DisplayGeometry geometry_;
  int topHeight_ = 0;
public:
  explicit DrmDisplay(const DisplayGeometry &g) : geometry_(g) {
    overlay_.setHole({0,0,g.width,g.height,g.width,g.height,g.rotation,0,0,g.xoffset,g.yoffset});
    if(!overlay_.open())throw std::runtime_error(overlay_.lastError());
  }
  bool present(const std::uint8_t *pixels,int w,int h) override {
    if(overlay_.presentBgra(pixels,w,topHeight_?topHeight_:h)) return true;
    if(overlay_.lastError().empty()) return false;
    throw std::runtime_error(overlay_.lastError());
  }
  void hide() override {overlay_.disable();}
  bool supportsTopRegion() const override { return true; }
  void topRegion(int height) override {
    topHeight_=height;
    overlay_.setHole({0,0,geometry_.width,height?height:geometry_.height,
      geometry_.width,geometry_.height,geometry_.rotation,0,0,
      geometry_.xoffset,geometry_.yoffset});
  }
};
}
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &g) {
  return std::unique_ptr<DisplaySurface>(new DrmDisplay(g));
}
}

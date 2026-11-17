#include "platform.hpp"
#include <cassert>
#include <unistd.h>
namespace { int made=0,attempts=0,hideCount=0;bool fatal=false; }
namespace camera {
class FixtureSurface final : public DisplaySurface {
public:
  bool present(const uint8_t *,int,int) override {
    ++attempts;
    if(fatal)throw std::runtime_error("fixture fatal geometry");
    if(attempts<4)throw DisplayUnavailable("fixture suspended display");
    return true;
  }
  void hide() override {++hideCount;}
};
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &) {
  ++made;return std::unique_ptr<DisplaySurface>(new FixtureSurface);
}
}
int main() {
  lv_init();penchat::Screen screen;screen.width=96;screen.height=64;
  penchat::Display display(screen,false);
  lv_refr_now(display.handle());assert(!display.available());
  display.visible(false);const int before=attempts;
  usleep(110000);display.check();assert(attempts==before&&made==1);
  display.visible(true);
  for(int i=0;i<5&&!display.available();++i){usleep(110000);display.check();}
  assert(display.available()&&display.presented()&&made==1&&hideCount==1);
  fatal=true;lv_obj_invalidate(lv_screen_active());lv_refr_now(display.handle());
  bool caught=false;try{display.check();}catch(const std::runtime_error &){caught=true;}
  assert(caught);
}

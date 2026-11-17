#include "platform.hpp"
#include <cassert>
#include <unistd.h>
namespace { int made=0,attempts=0,hideCount=0;bool fatal=false,upper=false,makeTemporary=false; }
namespace camera {
class FixtureSurface final : public DisplaySurface {
public:
  bool releaseOnHide() const override {return upper;}
  bool systemOverlaySafe() const override {return !upper;}
  bool present(const uint8_t *,int,int) override {
    ++attempts;
    if(fatal)throw std::runtime_error("fixture fatal geometry");
    if(attempts<4)throw DisplayUnavailable("fixture suspended display");
    return true;
  }
  void hide() override {++hideCount;}
};
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &) {
  ++made;
  if(makeTemporary){makeTemporary=false;DisplayInfo info;info.stage="primary_not_ready";throw DisplayInitializationError("fixture rebuilding primary",info);}
  return std::unique_ptr<DisplaySurface>(new FixtureSurface);
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
  fatal=false;upper=true;const int first=attempts;
  {
    penchat::Display legacy(screen,false);
    assert(!legacy.visible()&&!legacy.presented()&&!legacy.available());
    lv_refr_now(legacy.handle());assert(attempts==first);
    lv_display_set_default(legacy.handle());
    legacy.visible(true);assert(attempts>first&&legacy.presented());
    legacy.visible(false);makeTemporary=true;legacy.visible(true);
    assert(!legacy.available()&&legacy.pending());
    usleep(110000);legacy.check();assert(legacy.available()&&legacy.presented());
  }

}

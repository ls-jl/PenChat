#include "platform.hpp"
#include <cassert>
#include <iostream>
#include <vector>
namespace {
struct State {
  bool available=false,fail=false;
  unsigned attempts=0,accepted=0,hidden=0,created=0,destroyed=0;
  std::vector<std::uint8_t> lastAttempt,lastAccepted;
} state;
class Surface final:public camera::DisplaySurface {
public:
  Surface(){++state.created;}
  ~Surface(){++state.destroyed;}
  bool present(const std::uint8_t *pixels,int w,int h)override {
    ++state.attempts;state.lastAttempt.assign(pixels,pixels+std::size_t(w)*h*4);
    if(state.fail)throw std::runtime_error("injected display failure");
    if(!state.available)return false;
    ++state.accepted;state.lastAccepted=state.lastAttempt;return true;
  }
  void hide()override{++state.hidden;}
};
}
namespace camera {
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &) {
  return std::unique_ptr<DisplaySurface>(new Surface);
}
}
int main() {
  lv_init();
  penchat::Screen screen;screen.width=64;screen.height=48;
  {
    penchat::Display display(screen,false);
    auto root=lv_screen_active();
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    auto paint=[&](unsigned color){lv_obj_set_style_bg_color(root,lv_color_hex(color),0);lv_refr_now(display.handle());};
    paint(0xff0000);assert(display.pending()&&state.accepted==0);
    const auto older=state.lastAttempt;
    paint(0x00ff00);assert(display.pending()&&state.lastAttempt!=older);
    const auto latest=state.lastAttempt;
    display.check();assert(display.pending()&&state.accepted==0);
    state.available=true;display.check();
    assert(!display.pending()&&state.accepted==1&&state.lastAccepted==latest);
    const auto attempts=state.attempts;
    display.check();assert(state.attempts==attempts); // No idle copies or redraws.
    state.available=false;paint(0x0000ff);assert(display.pending());
    display.visible(false);assert(!display.pending()&&state.hidden==1);
    display.check();assert(state.accepted==1);
    display.mediaHandoff(true);assert(state.destroyed==1&&!display.pending());
    state.available=true;display.visible(true);
    assert(state.created==1); // Visibility cannot override a capture handoff.
    display.mediaHandoff(false);assert(state.created==2);
    lv_refr_now(display.handle());
    assert(!display.pending()&&state.accepted>=2);
    // Driver errors are reported outside the LVGL callback's C stack.
    state.fail=true;paint(0x909090);
    bool caught=false;try{display.check();}catch(const std::exception &){caught=true;}
    assert(caught&&!display.pending());
  }
  assert(state.created==state.destroyed);
  lv_deinit();penchat::shutdownFonts();
  std::cout<<"DISPLAY_DEFER_COALESCES_LATEST_AND_CLEANS_HANDOFF_OK\n";
}

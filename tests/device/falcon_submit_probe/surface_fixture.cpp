#include "surface.hpp"
#include "composite_wire.hpp"
#include "display_surface.h"
#include <cassert>
#include <unistd.h>
#include <vector>
int main() {
  using namespace penchat_compositor;
  configureDisplay(3,4);
  auto surface=camera::makeDisplaySurface({13,7,0,0,0,13,7});
  assert(surface->sharedCaptureSafe()&&!surface->releaseOnHide());
  std::vector<std::uint8_t> pixels(13*7*4);
  auto paint=[&](std::uint32_t color){for(std::size_t i=0;i<pixels.size();i+=4)__builtin_memcpy(pixels.data()+i,&color,4);surface->present(pixels.data(),13,7);};
  paint(0xff123456);
  const auto start=monotonicMs();bool hidden=false,resumed=false;
  while(std::uint32_t(monotonicMs()-start)<5000) {
    const auto elapsed=std::uint32_t(monotonicMs()-start);
    if(elapsed>=200&&!hidden){surface->hide();hidden=true;}
    if(elapsed>=500&&!resumed){paint(0xffabcdef);resumed=true;}
    surface->check();usleep(3000);
  }
}

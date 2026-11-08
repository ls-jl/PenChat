#include "display_surface.h"
#include "overlay.h"
#include <cassert>
#include <cerrno>
#include <iostream>
#include <stdexcept>
namespace { int failure=EBUSY, lastWidth=0,lastHeight=0;camera::HoleRect region; }
namespace camera {
Overlay::Overlay()=default;
Overlay::~Overlay()=default;
bool Overlay::open(){return true;}
void Overlay::setHole(const HoleRect &hole){region=hole;}
void Overlay::disable(){}
bool Overlay::presentBgra(const uint8_t *,int width,int height){
  lastWidth=width;lastHeight=height;error_.clear();errno=failure;
  if(failure&&failure!=EBUSY&&failure!=EAGAIN)error_="injected permanent DRM failure";
  return !failure;
}
}
int main(){
  auto surface=camera::makeDisplaySurface({568,1210,180,0,0,568,1210});
  uint8_t pixels[4]={};
  assert(!surface->present(pixels,568,1210));
  failure=EAGAIN;assert(!surface->present(pixels,568,1210));
  failure=0;assert(surface->present(pixels,568,1210));
  assert(surface->supportsTopRegion());surface->topRegion(700);
  assert(surface->present(pixels,568,1210));assert(lastWidth==568&&lastHeight==700);
  assert(region.w==568&&region.h==700&&region.panelH==1210&&region.rotation==180);
  surface->topRegion(0);assert(surface->present(pixels,568,1210));assert(lastHeight==1210);
  failure=EIO;bool caught=false;try{surface->present(pixels,568,1210);}catch(const std::runtime_error &e){caught=std::string(e.what()).find("permanent")!=std::string::npos;}
  assert(caught);std::cout<<"DRM_BUSY_RETRY_PARTIAL_REGION_AND_FATAL_ERROR_OK\n";
}

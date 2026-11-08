#include "composite.hpp"
#include <cassert>
#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <vector>
using namespace penchat_compositor;
template<class F>void waitFor(F f){const auto start=monotonicMs();while(!f()){assert(std::uint32_t(monotonicMs()-start)<2500);usleep(3000);}}
int main(int argc,char** argv) {
  assert(argc==3);
  auto* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(lib);
  auto present=reinterpret_cast<Present>(dlsym(lib,"fixture_submit"));
  auto pixel=reinterpret_cast<unsigned(*)()>(dlsym(lib,"fixture_pixel"));assert(present&&pixel);
  const auto* base=strrchr(argv[1],'/');auto slot=locate(base?base+1:argv[1],"fixture_present");
  auto& core=Composite::instance();
  // Production transport with no deadline and externally owned child.
  pid_t child=-1;
  core.start(slot,13,7,[&](int socket,int shared){child=spawnClient(argv[2],socket,shared);return child;});
  std::vector<std::uint8_t> system(13*7*4,0);const Rect all{0,0,13,7};
  core.focus(true);present(system.data(),all,13,7);
  waitFor([&]{core.focus(true);return pixel()==0xff123456;});
  waitFor([&]{core.focus(true);return pixel()==0;}); // hidden restores original system
  waitFor([&]{core.focus(true);return pixel()==0xffabcdef;});
  // The display lease remains alive when an attached system window owns input.
  // Its premultiplied pixels cover LVGL; transparent pixels retain the base.
  const auto revision=core.stats().nativeFrames;
  const unsigned cover=0x80402010;
  std::memcpy(system.data(),&cover,4);present(system.data(),all,13,7);
  std::uint8_t expected[4];const unsigned baseColor=0xffabcdef;
  blendOver(reinterpret_cast<const std::uint8_t*>(&cover),reinterpret_cast<const std::uint8_t*>(&baseColor),expected,1);
  unsigned blended=0;std::memcpy(&blended,expected,4);
  waitFor([&]{core.focus(true);return pixel()==blended;});
  assert(core.stats().nativeFrames==revision); // Overlay does not need an LVGL redraw.
  const unsigned opaque=0xff112233;
  std::memcpy(system.data(),&opaque,4);present(system.data(),all,13,7);
  waitFor([&]{core.focus(true);return pixel()==opaque;});
  std::fill(system.begin(),system.end(),0);present(system.data(),all,13,7);
  waitFor([&]{core.focus(true);return pixel()==0xffabcdef;});
  const auto frames=core.stats().outputFrames;
  for(int i=0;i<15;++i){core.focus(true);usleep(5000);}
  assert(core.stats().outputFrames==frames); // No redraw/idle copies.
  core.focus(false);
  waitFor([&]{return pixel()==0;});
  core.focus(true);
  waitFor([&]{core.focus(true);return pixel()==0xffabcdef;}); // redraw without new LVGL flush
  kill(child,SIGKILL);waitFor([&]{return !core.stats().running;});core.stop();
  assert(core.stats().originalRestored&&__atomic_load_n(slot.address,__ATOMIC_SEQ_CST)==slot.original);
  reapClient(child);dlclose(lib);
  std::cout<<"PRODUCTION_SURFACE_SYSTEM_OVERLAY_ALPHA_HIDE_RESUME_IDLE_AND_DISCONNECT_OK\n";
}

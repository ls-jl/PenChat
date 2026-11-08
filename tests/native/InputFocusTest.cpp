#include "input_focus.hpp"
#include <cassert>
#include <iostream>
int main() {
  penchat::InputFocus device(true);
  assert(!device.allowed(0));
  assert(device.update(1,true,100));assert(device.allowed(100));assert(device.allowed(399));
  assert(device.mediaAllowed(400)&&!device.mediaAllowed(1600));
  assert(!device.allowed(400)); // A blocked launcher cannot leave input enabled.
  assert(!device.update(1,true,410));assert(!device.allowed(410));
  assert(device.update(2,true,420));assert(device.allowed(420));
  assert(device.update(3,false,421));assert(!device.allowed(421)&&!device.mediaAllowed(421));
  assert(!device.update(2,true,422));assert(!device.allowed(422));
  assert(device.update(4,true,430));device.block();assert(!device.allowed(431));
  assert(device.update(5,true,440));assert(device.allowed(440));assert(!device.allowed(439));
  penchat::InputFocus host;assert(host.allowed(0));host.block();assert(host.allowed(1));
  assert(host.update(1,false,2));assert(!host.allowed(2));
  device.update(100,true,1000);
  auto grace=device.permissions(1301,true,true,false,false,false,false);
  assert(!grace.input&&grace.media&&grace.display);
  auto drm=device.permissions(1001,true,true,false,false,false,false);
  assert(drm.input&&drm.media&&drm.display);
  drm=device.permissions(1001,true,false,false,false,false,false);
  assert(!drm.input&&!drm.media&&!drm.display);
  auto composite=device.permissions(1001,true,false,false,false,true,false);
  assert(!composite.input&&!composite.media&&composite.display);
  auto official=device.permissions(1001,true,true,true,true,false,false);
  assert(!official.input&&!official.media&&official.display);
  auto hidden=device.permissions(1001,false,true,false,false,true,false);
  assert(!hidden.input&&!hidden.media&&!hidden.display);
  std::cout<<"INPUT_FOCUS_LEASE_EXPIRES_REJECTS_STALE_AND_FAILS_CLOSED_OK\n";
}

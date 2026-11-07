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
  std::cout<<"INPUT_FOCUS_LEASE_EXPIRES_REJECTS_STALE_AND_FAILS_CLOSED_OK\n";
}

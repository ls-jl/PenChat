#include "cvi_frame_slots.h"
#include <array>
#include <cassert>
#include <iostream>
int main() {
  camera::cvi::FrameSlots<3> slots;
  std::array<unsigned,3> users{{1,1,1}};
  auto choose=[&]{return slots.writable([&](std::size_t n){return users[n];});};
  assert(choose()==0);users[0]=2; // Driver is still scanning out frame zero.
  assert(choose()==1);users[1]=2;
  assert(choose()==2);users[2]=2;
  for(int i=0;i<20;++i)assert(choose()==-1); // Slow consumer; no unsafe rewrite.
  assert(slots.deferred==20);
  users[1]=1;assert(choose()==1);users[1]=3; // Hardware completed only this slot.
  assert(choose()==-1);
  users[2]=1;assert(choose()==2);users[2]=2;
  users[0]=0;assert(choose()==-1); // A missing application ref is not writable.
  users[0]=1;assert(choose()==0);
  assert(slots.busyChecks>=60);
  std::cout<<"CVI_FRAME_SLOTS_NEVER_REWRITE_DRIVER_REFERENCES_OK\n";
}

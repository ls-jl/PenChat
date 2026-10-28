#include "native_touch.hpp"
#include <linux/input.h>
#include <cassert>
#include <iostream>
using namespace penchat;
int main() {
  Screen s; s.width=960; s.height=266; s.touchRotation=270; s.touchXoffset=113;
  TouchFrames f(s); f.reset(std::vector<TouchFrames::Slot>(4));
  auto abs=[&](int c,int v){assert(f.event(EV_ABS,c,v).empty());};
  auto sync=[&]{return f.event(EV_SYN,SYN_REPORT,0);};
  auto down=[&](int slot,int id,int x,int y){
    abs(ABS_MT_SLOT,slot);abs(ABS_MT_TRACKING_ID,id);
    abs(ABS_MT_POSITION_X,y+113);abs(ABS_MT_POSITION_Y,s.width-1-x);
  };
  auto up=[&](int slot){abs(ABS_MT_SLOT,slot);abs(ABS_MT_TRACKING_ID,-1);};
  uint64_t serial=0;
  // Rapid alternating slots must always start at their own position and end there.
  for(int i=0;i<1000;++i) {
    const int slot=i%4,x=i%2?550:3,y=110;
    down(slot,i+1,x,y);auto p=sync();assert(p.size()==1);
    assert(p[0]["pressed"]==true&&p[0]["x"]==x&&p[0]["y"]==y);
    assert(p[0]["gesture"].get<uint64_t>()>serial);serial=p[0]["gesture"];
    up(slot);p=sync();assert(p.size()==1&&!p[0]["pressed"].get<bool>());
    assert(p[0]["x"]==x&&p[0]["y"]==y&&p[0]["gesture"]==serial);
    assert(sync().empty());
  }
  down(0,2001,10,80);assert(sync().size()==1);
  up(0);down(3,2002,700,120);auto p=sync();
  assert(p.size()==2&&p[0]["pressed"]==false&&p[0]["x"]==10&&p[1]["x"]==700&&p[1]["pressed"]==true);
  down(1,2003,400,100);p=sync();assert(p.size()==1&&p[0]["points"].size()==2);
  auto gesture=p[0]["gesture"];auto remaining=p[0]["points"][1]["id"];
  up(1);p=sync();assert(p[0]["gesture"]==gesture&&p[0]["points"][0]["id"]==remaining);
  p=f.event(EV_SYN,SYN_DROPPED,0);assert(p.size()==1&&p[0]["cancelled"]==true&&f.dropped());
  assert(sync().empty());assert(f.event(EV_ABS,ABS_MT_POSITION_X,200).empty());
  f.reset(std::vector<TouchFrames::Slot>(4),0,true);
  down(1,2004,20,100);assert(sync().empty());up(1);assert(sync().empty());
  down(2,2005,30,100);p=sync();assert(p.size()==1&&p[0]["x"]==30);up(2);sync();
  // Bezel touches outside the configured logical screen never become edge clicks.
  down(0,2006,-1,100);assert(sync().empty());up(0);assert(sync().empty());
  down(1,2007,40,100);assert(sync().size()==1);
  abs(ABS_MT_POSITION_X,112);p=sync();assert(p.size()==1&&p[0]["pressed"]==true&&p[0]["y"]==-1);
  up(1);p=sync();assert(p.size()==1&&p[0]["pressed"]==false&&p[0]["y"]==-1);
  // Startup while held, lifecycle reset, invalid slots, sparse coordinate updates.
  std::vector<TouchFrames::Slot> held(4);held[3]={2008,213,919};f.reset(held,3);
  assert(sync().empty());up(3);assert(sync().empty());
  abs(ABS_MT_SLOT,99);abs(ABS_MT_TRACKING_ID,123);assert(sync().empty());
  down(0,2009,41,100);assert(sync()[0]["x"]==41);up(0);sync();
  abs(ABS_MT_TRACKING_ID,2010);abs(ABS_MT_POSITION_Y,917);p=sync();
  assert(p[0]["x"]==42&&p[0]["y"]==100);up(0);sync();
  f.reset({});assert(sync().empty());
  for(int rotation:{0,90,180,270}) {
    s.touchRotation=rotation;s.touchYoffset=7;
    const int x=37,y=49,rx=rotation==270?y:rotation==180?s.width-1-x:rotation==90?s.height-1-y:x;
    const int ry=rotation==270?s.width-1-x:rotation==180?s.height-1-y:rotation==90?x:y;
    assert(logicalTouch(s,rx+113,ry+7)==std::make_pair(x,y));
  }
  NativeTouch inactive(s,false);assert(inactive.poll(true).empty()&&!inactive.active());
  std::cout<<"Native touch: 1000 alternating-slot taps, frame boundaries, releases, replacement, MT, overrun, bezel, lifecycle and rotations passed.\n";
}

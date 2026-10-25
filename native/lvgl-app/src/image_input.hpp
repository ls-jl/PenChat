#pragma once
#include "common.hpp"
namespace penchat {
inline std::pair<int,int> logicalImageTouch(const Screen &s,int rawX,int rawY) {
  const int x=rawX-s.touchXoffset,y=rawY-s.touchYoffset;
  if(s.touchRotation==270)return {s.width-1-y,x};
  if(s.touchRotation==180)return {s.width-1-x,s.height-1-y};
  if(s.touchRotation==90)return {y,s.height-1-x};
  return {x,y};
}
// Read-only, nonexclusive MT reader, alive only in a foreground image workspace.
// Falcon on these firmwares collapses MT slots into one page touch.
class DirectImageInput {
  struct Slot { int id=-1,x=0,y=0; };
  Screen screen_;
  int fd_=-1,slot_=0,lastCount_=0;
  std::vector<Slot> slots_;
  bool waitClear_=false;
  uint64_t serial_=uint64_t(1)<<48;
  void open();
  void resync();
public:
  explicit DirectImageInput(const Screen &s):screen_(s){}
  ~DirectImageInput(){close();}
  void close();
  bool active() const{return fd_>=0;}
  void cancel(){resync();waitClear_=true;lastCount_=0;}
  std::vector<Json> poll(bool enabled);
};
} // namespace penchat

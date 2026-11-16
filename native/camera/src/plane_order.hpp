#pragma once
#include <algorithm>
#include <cstdint>
namespace camera {
struct PlaneZRange { uint64_t low,high,current;bool writable; };
struct UnderlayOrder { uint64_t native,host; };
inline bool underlayOrder(const PlaneZRange &native,const PlaneZRange &host,UnderlayOrder &out) {
  if(native.low>native.high||host.low>host.high||host.current<host.low||host.current>host.high||native.current<native.low||native.current>native.high)return false;
  out.native=native.writable?native.low:native.current;out.host=host.current;
  if(out.native>=out.host) {
    if(!host.writable||out.native>=host.high)return false;
    out.host=std::max(host.low,out.native+1);
  }
  return out.native<out.host&&out.host<=host.high;
}
}

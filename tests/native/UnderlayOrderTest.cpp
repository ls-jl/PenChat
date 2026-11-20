#include "plane_order.hpp"
#include <cassert>
#include <limits>
int main() {
  camera::UnderlayOrder value{};
  assert(camera::underlayOrder({0,3,1,true},{0,3,0,true},value)&&value.native==0&&value.host==1);
  assert(camera::underlayOrder({0,3,2,true},{0,3,3,false},value)&&value.native==0&&value.host==3);
  assert(camera::underlayOrder({0,0,0,false},{1,1,1,false},value));
  assert(!camera::underlayOrder({0,3,2,true},{0,0,0,false},value));
  assert(!camera::underlayOrder({3,3,3,false},{0,3,0,true},value));
  assert(!camera::underlayOrder({0,3,4,true},{0,3,2,true},value));
  assert(camera::upperOverlayOrder({0,5,0,true},{0,3,3,false},value)&&value.native==4&&value.host==3);
  assert(!camera::upperOverlayOrder({0,3,0,true},{0,3,3,false},value));
  assert(!camera::upperOverlayOrder({0,5,0,false},{0,3,3,false},value));
  assert(camera::upperOverlayOrder({4,4,4,false},{0,3,3,false},value));
  const auto max=std::numeric_limits<uint64_t>::max();
  assert(!camera::underlayOrder({max,max,max,false},{0,max,max,true},value));
  assert(!camera::upperOverlayOrder({0,max,0,true},{0,max,max,false},value));
}

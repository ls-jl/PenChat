#include "sequence_gap.hpp"
#include <cassert>
#include <initializer_list>
using penchat_scan::recoverableSequenceGap;
int main() {
  for(unsigned distance=2;distance<=9;++distance) {
    assert(recoverableSequenceGap(distance,1));
    assert(recoverableSequenceGap(distance,150000));
  }
  for(unsigned distance:{0u,1u,10u,11u,0xffffffffu})
    assert(!recoverableSequenceGap(distance,8400));
  assert(!recoverableSequenceGap(2,0));
  assert(!recoverableSequenceGap(2,-1));
}

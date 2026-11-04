#include "sequence_gap.hpp"
#include <cassert>
using penchat_scan::recoverableSequenceGap;
int main() {
  assert(recoverableSequenceGap("y02-sc035gs", 2, 16800, 16));
  assert(recoverableSequenceGap("y02-sc035gs", 3, 25200, 16));
  assert(recoverableSequenceGap("y02-sc035gs", 4, 33600, 16));
  assert(recoverableSequenceGap("y02-sc035gs", 4, 40000, 16));
  assert(recoverableSequenceGap("y02-sc035gs", 4, 40001, 16));
  assert(recoverableSequenceGap("y02-sc035gs", 5, 35000, 16));
  assert(recoverableSequenceGap("y02-sc035gs", 2, 16800, 0));
  assert(recoverableSequenceGap("y02-sc035gs", 8, 67200, 0));
  assert(recoverableSequenceGap("y02-sc035gs", 8, 150000, 0));
  assert(!recoverableSequenceGap("y02-sc035gs", 9, 75600, 32));
  assert(!recoverableSequenceGap("y02-sc035gs", 10, 84000, 32));
  assert(!recoverableSequenceGap("y02-sc035gs", 0, 8400, 32));
  assert(!recoverableSequenceGap("y02-sc035gs", 0xffffffff, 8400, 32));
  assert(!recoverableSequenceGap("y02-sc035gs", 2, 0, 32));
  assert(!recoverableSequenceGap("y02-sc035gs", 2, -1, 32));
  for (const char *profile : {"orange-sc2356", "y07-sc2356", "unknown"}) {
    assert(recoverableSequenceGap(profile, 2, 35000, 16));
    assert(!recoverableSequenceGap(profile, 3, 25000, 16));
    assert(!recoverableSequenceGap(profile, 2, 35001, 16));
    assert(!recoverableSequenceGap(profile, 2, 16800, 15));
  }
}

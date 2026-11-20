#include "capture_timing.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace penchat_scan;
int main() {
  assert(scanKeyTimestamp(9,975000,true,10000000,1000000000)==9975000);
  assert(scanKeyTimestamp(999,975000,false,10000000,1000000000)==9975000);
  assert(scanKeyTimestamp(11,0,true,10000000,1000000000)==10000000);
  assert(scanKeyTimestamp(0,0,true,10000000,1000000000)==10000000);
  assert(scanKeyTimestamp(INT64_MAX,0,true,10000000,1000000000)==10000000);
  assert(!afterReleasedFrame(25,25,true,false)); // Inclusive boundary.
  assert(afterReleasedFrame(26,25,true,false));
  assert(!afterReleasedFrame(26,25,true,true)); // Exposure-gated backend unchanged.
  assert(!afterReleasedFrame(26,25,false,false));
  int wall=0, reads=0, polledAtRelease=0, cutoff=0;
  bool draining=false, sealed=false;
  std::vector<std::int64_t> accepted;
  const std::vector<std::int64_t> timestamps{20,24,40,60};
  pollCaptureBatch([&] {
      if(wall>=25&&!draining&&!sealed){draining=true;cutoff=25;polledAtRelease=wall;}
    }, [&](const auto& consume) {
      if(reads==int(timestamps.size()))return false;
      wall+=20;auto timestamp=timestamps[reads++];consume(nullptr,0,reads,timestamp);return true;
    }, [&](const std::uint8_t*,int,std::uint64_t,std::int64_t timestamp) {
      if(sealed)return;
      if(afterReleasedFrame(timestamp,cutoff,draining,false)){sealed=true;draining=false;return;}
      accepted.push_back(timestamp);
    }, [] {return true;});
  assert(polledAtRelease==40); // One blocking read, not sixteen (320ms).
  assert((accepted==std::vector<std::int64_t>{20,24})); // Keep queued pre-release pixels.
  assert(sealed);
  // Pending release is checked even when the next read would block/no-frame.
  int polls=0, attempts=0;
  pollCaptureBatch([&]{++polls;},[&](const auto&){++attempts;return false;},
      [](const std::uint8_t*,int,std::uint64_t,std::int64_t){assert(false);},[]{return true;});
  assert(polls==1&&attempts==1);
  attempts=0;
  pollCaptureBatch([]{},[&](const auto&){++attempts;return true;},
      [](const std::uint8_t*,int,std::uint64_t,std::int64_t){},[]{return false;});
  assert(attempts==0); // Drain deadline checked before another backend wait.
  std::cout<<"SCAN_KEY_TIME_AND_PER_FRAME_RELEASE_BOUNDARY_OK\n";
}

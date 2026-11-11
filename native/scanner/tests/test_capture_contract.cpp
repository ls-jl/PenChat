#include "capture_stream.hpp"
#include "cvi_startup.hpp"
#include "optical_config.hpp"
#include "start_policy.hpp"
#include <cassert>
#include <climits>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace penchat_scan;
template<class F> void rejects(F f) {
  bool rejected=false;try {f();} catch (...) {rejected=true;} assert(rejected);
}
int main() {
  assert(cviStartupCanReopen(0,int(cviBufferEmpty)));
  assert(!cviStartupCanReopen(1,int(cviBufferEmpty)));
  assert(!cviStartupCanReopen(0,0));
  assert(!cviStartupCanReopen(0,int(0xc0068010u)));
  unsigned opens=0,notices=0;
  auto notify=[&](const char *){++notices;};
  assert(openCviWithRecovery([&]{++opens;return 9;},notify)==9);
  assert(opens==1&&notices==0);
  opens=notices=0;
  assert(openCviWithRecovery([&]{if(++opens==1)throw CviStartupEmpty("zero frames");return 7;},notify)==7);
  assert(opens==2&&notices==1);
  opens=notices=0;
  rejects([&]{openCviWithRecovery([&]()->int{++opens;throw CviStartupEmpty("still empty");},notify);});
  assert(opens==2&&notices==1); // No third attempt or unbounded hardware loop.
  opens=notices=0;
  rejects([&]{openCviWithRecovery([&]()->int{++opens;throw std::runtime_error("invalid frame or cleanup failure");},notify);});
  assert(opens==1&&notices==0);
  const auto s6=scanProfileById("s6-pag7920");
  std::vector<std::uint8_t> native(336*240,23);
  FrameView source{native.data(),native.size(),320,240,336,77,123456};
  auto paper=paperFrame(source,s6);
  assert(paper.y==native.data()+8*336+112 && paper.width==160 && paper.height==192);
  assert(paper.stride==336 && paper.sequence==77 && paper.timestamp==123456);
  native[8*336+112]=91;assert(paper.y[0]==91); // A borrowed view, no extra copy.
  auto shortFrame=source;shortFrame.bytes=336*239+319;rejects([&]{paperFrame(shortFrame,s6);});
  auto invalid=source;invalid.stride=319;rejects([&]{validateFrame(invalid);});
  invalid=source;invalid.y=nullptr;rejects([&]{validateFrame(invalid);});
  invalid=source;invalid.width=319;rejects([&]{paperFrame(invalid,s6);});
  invalid=source;invalid.stride=INT_MAX;invalid.height=INT_MAX;
  rejects([&]{validateFrame(invalid);}); // 64-bit required-size arithmetic on ARM32 too.
  const auto config=opticalConfig(s6,"/missing");
  assert(config.width==160 && config.height==192 && config.rotation==pen::stream::Rotation::None);
  assert(config.calibration->gain[0]==1.f);
  auto custom=scanProfileById("y02-sc035gs");custom.id="test-new-device";
  custom.width=128;custom.height=64;custom.optics.version="test-native-neutral";
  auto alternate=opticalConfig(custom,"/missing");
  assert(alternate.width==128 && alternate.height==64 && alternate.calibration->version=="test-native-neutral");
  assert(alternate.rotation==pen::stream::Rotation::CounterClockwise90);
  rejects([&]{opticalConfig(scanProfileById("y07-sc2356"),"/missing");});
  char directory[]="/tmp/penchat-optical-XXXXXX";assert(mkdtemp(directory));
  const auto path=std::string(directory)+"/constant.json";
  custom.optics.calibration=CalibrationKind::ConstantFile;custom.optics.metadata="constant.json";
  nlohmann::json meta={{"profile",custom.id},{"sensor_width",128},{"sensor_height",64},
    {"rotation","counterclockwise90"},{"version","test-constant"},{"gain_constant",1.1}};
  auto save=[&]{std::ofstream f(path);f<<meta;};save();
  assert(opticalConfig(custom,directory).calibration->gain[0]==1.1f);
  meta["sensor_width"]=127;save();rejects([&]{opticalConfig(custom,directory);});
  meta["sensor_width"]=128;meta["gain_constant"]=2;save();rejects([&]{opticalConfig(custom,directory);});
  unlink(path.c_str());rmdir(directory);

  std::vector<int> calls;
  auto control=[&](StreamControl c){calls.push_back(int(c));};
  auto sleep=[&](unsigned ms){calls.push_back(int(ms));};
  auto stable=[&](unsigned ms){calls.push_back(int(ms));return StartupFrames::Stable;};
  startRkCapture(StartupPolicy::DoubleQuick,control,sleep,stable);
  assert((calls==std::vector<int>{0,1,200,2,100,2}));
  calls.clear();startRkCapture(StartupPolicy::SingleQuickStable,control,sleep,stable);
  assert((calls==std::vector<int>{0,1,50,2,2000}));
  calls.clear();startRkCapture(StartupPolicy::StandardFirst,control,sleep,stable);
  assert((calls==std::vector<int>{1,2000}));
  calls.clear();unsigned waits=0;
  startRkCapture(StartupPolicy::StandardFirst,control,sleep,[&](unsigned ms){
    calls.push_back(int(ms));return ++waits==1?StartupFrames::None:StartupFrames::Stable;
  });
  assert((calls==std::vector<int>{1,2000,2,2000}));
  calls.clear();rejects([&]{startRkCapture(StartupPolicy::StandardFirst,control,sleep,
    [](unsigned){return StartupFrames::Unstable;});});
  assert((calls==std::vector<int>{1})); // No retry after a real but unstable source.
  for (int failure=0;failure<4;++failure) {
    unsigned controls=0;
    rejects([&]{startRkCapture(StartupPolicy::DoubleQuick,[&](StreamControl){
      if (controls++==unsigned(failure)) throw std::runtime_error("injected driver error");
    },[](unsigned){},[](unsigned){return StartupFrames::Stable;});});
    assert(controls==unsigned(failure+1)); // Never retry or continue after failed IO.
  }
  std::cout<<"SCAN_FRAME_OPTICS_AND_STARTUP_CONTRACT_OK\n";
}

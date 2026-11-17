#include "ScreenPower.hpp"
#include "input_focus.hpp"
#include <atomic>
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

int main() {
  char temporary[]="/tmp/penchat-power-XXXXXX";assert(mkdtemp(temporary));
  const std::string root=temporary,node=root+"/display",file=node+"/bl_power";
  assert(mkdir(node.c_str(),0700)==0);
  auto write=[&](const std::string &value){
    {std::ofstream out(file+".new");out<<value;}
    assert(rename((file+".new").c_str(),file.c_str())==0);
  };
  write("0\n");auto state=penchat::ScreenPower::read(root);assert(state.available&&state.known&&state.awake);
  // Low/zero brightness never overrides an explicitly unblanked power state.
  {std::ofstream brightness(node+"/actual_brightness");brightness<<"0\n";}
  assert(penchat::ScreenPower::read(root).awake);
  for(const auto &text:{"1\n","2\n","3\n","4\n"}) {
    write(text);state=penchat::ScreenPower::read(root);assert(state.available&&state.known&&!state.awake);
  }
  for(const auto &text:{"","5\n","-1\n","0x0","0junk","overflow999999999999999999999999999999999"}) {
    write(text);state=penchat::ScreenPower::read(root,true);assert(state.available&&!state.known&&!state.awake);
  }
  unlink(file.c_str());state=penchat::ScreenPower::read(root,true);assert(state.available&&!state.known);
  write("4\n");
  std::atomic<int> notifications{0};penchat::ScreenPower monitor;
  monitor.start([&](const penchat::ScreenPower::State &){if(++notifications==1)throw std::runtime_error("listener failure");},root);
  auto wait=[&](bool awake){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(std::chrono::steady_clock::now()<end) {
      auto s=monitor.state();if(s.known&&s.awake==awake&&notifications>=2)return;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false);
  };
  wait(false);write("0\n");wait(true);
  unlink(file.c_str());
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  assert(monitor.state().available&&!monitor.state().known);
  monitor.stop();assert(!monitor.running());
  unlink((node+"/actual_brightness").c_str());rmdir(node.c_str());rmdir(root.c_str());
  penchat::ScreenPowerGate gate;
  assert(!gate.blocked(0));
  assert(gate.update(1,true,true,false,100));assert(gate.blocked(100));
  assert(!gate.update(1,true,true,true,101)&&gate.blocked(101));
  assert(gate.update(2,true,true,true,200));assert(!gate.blocked(1699)&&gate.blocked(1700));
  assert(gate.update(3,true,false,false,1800)&&gate.blocked(1800));
  assert(gate.update(4,true,true,true,1900)&&!gate.blocked(1900)&&gate.blocked(1899));
  assert(!gate.update(0,false,false,false,2000));
  std::cout<<"SCREEN_POWER_READ_ONLY_SIGNAL_MONITOR_RECOVERY_AND_GATE_OK\n";
}

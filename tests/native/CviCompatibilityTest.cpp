#include "../../native/platform/identity.hpp"
#include "../../native/compositor/firmware.hpp"
#include "../../native/scanner/src/profile.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main(){
  using namespace platform;
  for(const auto &model:{"Cvitek Cv1826 for Youdao Plum V0 version","Cvitek Cv1826 for OtherBoard V2","Vendor CV182X board","SOPHGO cv1835","cv180x","cv1812","cv1813h","cv1800b"}){
    auto p=identify(model);assert(p.family==Family::Cvi&&std::string(p.packageTarget())=="cv1826-s");
  }
  const char rawCompatible[]="vendor,newboard\0cvitek,cv1826\0";
  const std::string compatible(rawCompatible,sizeof(rawCompatible)-1);
  assert(identify("Other model",compatible).family==Family::Cvi);
  for(const auto &model:{"notcv1826","cv18260","cv1826fake","cvi","Plum","RISC-V unknown","cv1",""})assert(identify(model).family==Family::Unknown);
  assert(identify("RK3562 Y02","cvitek,cv1826").family==Family::Ambiguous);
  assert(identify("RK3562 Y07").family==Family::Rockchip);
  assert(identify("prefixrk3562").family==Family::Unknown);
  const auto &profiles=penchat_compositor::firmwareProfiles();assert(!profiles.empty());
  for(const auto &p:profiles){assert(penchat_compositor::firmwareProfile(p.sha256,p.bits,p.machine)==&p);
    assert(!penchat_compositor::firmwareProfile(p.sha256,64,183));assert(!penchat_compositor::firmwareProfile(p.sha256,32,243));}
  assert(!penchat_compositor::firmwareProfile(std::string(64,'0'),32,40));
  // A common screen/ABI does not license S6's optics, lamp or sensor pipeline.
  assert(!penchat_scan::scanProfile("Cvitek Cv1826 for OtherBoard V2","PIXART_PAG7920_MIPI_240P_100FPS_10BIT").supported());
  const auto s6=penchat_scan::scanProfile("Cvitek Cv1826 for Youdao Plum V0 version","PIXART_PAG7920_MIPI_240P_100FPS_10BIT");assert(s6.supported());auto roi=penchat_scan::sensorRegion(s6);assert(roi.left==112&&roi.top==8&&roi.width==160&&roi.height==192);
  std::cout<<"CVI_FAMILY_FIRMWARE_ABI_AND_INDEPENDENT_SCAN_ADMISSION_OK\n";
}

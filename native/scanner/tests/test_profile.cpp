#include "board_config.hpp"
#include "start_policy.hpp"
#include <cassert>
#include <iostream>
using namespace penchat_scan;
int main() {
  const auto s6=scanProfile("Cvitek Cv1826 for Youdao Plum V0 version","PIXART_PAG7920_MIPI_240P_100FPS_10BIT");
  assert(s6.id=="s6-pag7920"&&s6.platform==PlatformKind::Cvi&&s6.width==320&&s6.height==240&&!s6.gatesExposure());
  assert(s6.light==ScanLight::ConfiguredGpio&&s6.videoNode.empty());
  const auto region=sensorRegion(s6);
  assert(region.left==112&&region.top==8&&region.width==160&&region.height==192);
  auto badRegion=s6;badRegion.opticalRegion.width=321;
  bool bad=false;try{sensorRegion(badRegion);}catch(...){bad=true;}assert(bad);
  assert(!scanProfile("unknown Plum","PIXART_PAG7920_MIPI_240P_100FPS_10BIT").supported());
  assert(!scanProfile("Cvitek Cv1826 for Youdao Plum V0 version","sc2356").supported());
  const auto orange = scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "m00_f_sc2356 5-0030");
  const auto y02 = scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "m00_f_sc035gs 5-0030");
  assert(orange.id == "orange-sc2356" && orange.width == 496 && orange.height == 360);
  assert(y02.id == "y02-sc035gs" && y02.width == 640 && y02.height == 480);
  assert(sensorRegion(y02).width==640&&sensorRegion(y02).height==480);
  assert(sensorRegion(orange).width==496&&sensorRegion(orange).height==360);
  const auto y07=scanProfile("Rockchip RK3562 Y07 LP4 V10 Board","sc2356");
  assert(y07.id=="y07-sc2356"&&y07.width==752&&y07.height==440);
  assert(y07.light==ScanLight::LedClass&&!y07.ledBrightness.empty());
  assert(y07.ledOnValue=="1"); // Vendor white-light mode, never LED_FULL (255).
  assert(y07.inference.privateLibrary=="librknnrt-y07.so");
  assert(y02.inference.privateLibrary.empty()&&orange.inference.privateLibrary.empty());
  assert(y02.light==ScanLight::Gpio&&!y02.gatesExposure());
  assert(orange.gatesExposure()&&y07.gatesExposure());
  assert(scanProfile("ORANGE", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "sc035gs").supported());
  assert(!scanProfile("Rockchip RK3562 Y07 LP4 Board", "sc035gs").supported());
  assert(!scanProfile("Rockchip RK3562 Y02-1 LP4 Board", "sc035gs").supported());
  assert(!scanProfile("unknown", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "").supported());
  auto melon=scanProfile("Rockchip RK3562 MELON LP4 V10 Board","m00_b_ov7251 5-0060");
  assert(melon.id=="melon-ov7251" && melon.width==640 && melon.height==480);
  assert(melon.capture.startup==StartupPolicy::StandardFirst && !melon.gatesExposure() && melon.videoNode=="/dev/video12");
  assert(melon.light==ScanLight::ConfiguredGpio && melon.inference.privateLibrary.empty());
  assert(!scanProfile("Rockchip RK3562 MELON LP4 Board","ov7251x").supported());
  assert(!scanProfile("Rockchip RK3562 MELON2 LP4 Board","ov7251").supported());
  assert(!scanProfile("Rockchip RK3562 MELON LP4 Board","sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 ORANGE LP4 Board","ov7251").supported());
  assert(y07.handoff.nativeOcrKeys && !orange.handoff.nativeOcrKeys);
  assert(s6.handoff.mediaHandoff && s6.handoff.blockingStandbyRead);
  for (const auto &entry:scanProfiles()) validateProfile(entry.profile);
  auto custom=y02;
  custom.id="test-new-device"; custom.width=672; custom.height=512;
  custom.inference.compatibility=RuntimeCompatibility::None;
  custom.optics.version="test-neutral";
  validateProfile(custom);
  assert(sensorRegion(custom).width==672 && requiredScanResources(custom).size()==1);
  for (int fault=0;fault<4;++fault) {
    auto invalid=custom;
    if (fault==0) invalid.inference.runtime=RuntimeKind::Cvi;
    if (fault==1) invalid.capture.boundary=ScanBoundary::HardwareGate;
    if (fault==2) invalid.light=ScanLight::LedClass;
    if (fault==3) invalid.opticalRegion={670,0,4,4};
    bool rejected=false;try {validateProfile(invalid);}catch (...) {rejected=true;}
    assert(rejected);
  }
  assert(scanProfileById("y02-sc035gs").width==640 && !scanProfileById("unknown").supported());
  using Json=nlohmann::json;
  const Json s6keys={{"Plum_V0",{{"head_key_id","platform-led_control-event"},
    {"head_key_code","134"},{"home_key_code","139"}}}};
  const Json s6leds={{"Plum_V0",{{"ir_gpio_number","491"},{"white_gpio_number","492"}}}};
  const auto s6configured=boardConfig(s6,s6keys,s6leds);
  assert(s6configured.gpio==492 && s6configured.keyCode==134 && s6configured.homeCode==139);
  auto s6Rejected=[&](Json k,Json l) {bool failed=false;try{boardConfig(s6,k,l);}catch(...){failed=true;}assert(failed);};
  auto badS6=s6leds;badS6["Plum_V0"]["white_gpio_number"]="491";s6Rejected(s6keys,badS6);
  badS6=s6leds;badS6["Plum_V0"].erase("white_gpio_number");s6Rejected(s6keys,badS6);
  badS6=s6leds;badS6["Plum_V0"]["white_gpio_number"]="-1";s6Rejected(s6keys,badS6);
  badS6=s6keys;badS6["Plum_V0"]["head_key_id"]="unknown";s6Rejected(badS6,s6leds);
  Json keys={{"RK3562_Melon_V0",{{"head_key_id","platform-led_control-event"},{"head_key_code","134"},
    {"home_key_id","platform-adc-keys-event"},{"home_key_code","102"}}}};
  Json leds={{"RK3562_Melon_V0",{{"ir_gpio_number","43"},{"white_gpio_number","44"}}}};
  auto configured=boardConfig(melon,keys,leds);
  assert(configured.keyCode==134 && configured.homeCode==102 && configured.gpio==43);
  auto rejected=[&](Json k,Json l) {bool failed=false;try{boardConfig(melon,k,l);}catch(...){failed=true;}assert(failed);};
  rejected(Json::object(),leds);
  auto invalid=keys;invalid["RK3562_Melon_V0"]["home_key_code"]="87";rejected(invalid,leds);
  invalid=leds;invalid["RK3562_Melon_V0"]["ir_gpio_number"]="-1";rejected(keys,invalid);
  invalid=leds;invalid["RK3562_Melon_V0"]["ir_gpio_number"]="44";rejected(keys,invalid);
  unsigned quick=0,waits=0;
  startOv7251([&](unsigned ms){assert(ms==2000);++waits;return StartupFrames::Stable;},[&]{++quick;});
  assert(waits==1 && quick==0);
  waits=0;startOv7251([&](unsigned){return ++waits==1?StartupFrames::None:StartupFrames::Stable;},[&]{++quick;});
  assert(waits==2 && quick==1);
  for(auto state:{StartupFrames::None,StartupFrames::Unstable}) {
    waits=quick=0;bool failed=false;
    try{startOv7251([&](unsigned){++waits;return state;},[&]{++quick;});}catch(...){failed=true;}
    assert(failed && waits==(state==StartupFrames::None?2u:1u) && quick==(state==StartupFrames::None?1u:0u));
  }
  waits=quick=0;
  try {
    startOv7251([&](unsigned)->StartupFrames {++waits;throw std::runtime_error("bad NV12 frame");},[&]{++quick;});
    assert(false);
  } catch(const std::runtime_error &) {assert(waits==1 && quick==0);}
  std::cout << "Scanner board/sensor profiles: explicit full-resolution capture and unknown/mismatch rejection passed\n";
}

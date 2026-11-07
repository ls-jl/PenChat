#include "melon_config.hpp"
#include "start_policy.hpp"
#include <cassert>
#include <iostream>
using namespace penchat_scan;
int main() {
  const auto s6=scanProfile("Cvitek Cv1826 for Youdao Plum V0 version","PIXART_PAG7920_MIPI_240P_100FPS_10BIT");
  assert(s6.id=="s6-pag7920"&&s6.cvi&&s6.width==320&&s6.height==240&&!s6.gatesExposure);
  assert(s6.light==ScanLight::ConfiguredGpio&&s6.videoNode.empty());
  assert(!scanProfile("unknown Plum","PIXART_PAG7920_MIPI_240P_100FPS_10BIT").supported());
  assert(!scanProfile("Cvitek Cv1826 for Youdao Plum V0 version","sc2356").supported());
  const auto orange = scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "m00_f_sc2356 5-0030");
  const auto y02 = scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "m00_f_sc035gs 5-0030");
  assert(orange.id == "orange-sc2356" && orange.width == 496 && orange.height == 360);
  assert(y02.id == "y02-sc035gs" && y02.width == 640 && y02.height == 480);
  const auto y07=scanProfile("Rockchip RK3562 Y07 LP4 V10 Board","sc2356");
  assert(y07.id=="y07-sc2356"&&y07.width==752&&y07.height==440);
  assert(y07.light==ScanLight::LedClass&&!y07.ledBrightness.empty());
  assert(y07.ledOnValue=="1"); // Vendor white-light mode, never LED_FULL (255).
  assert(y07.privateRuntime=="librknnrt-y07.so");
  assert(y02.privateRuntime.empty()&&orange.privateRuntime.empty());
  assert(y02.light==ScanLight::Gpio43&&!y02.gatesExposure);
  assert(orange.gatesExposure&&y07.gatesExposure);
  assert(scanProfile("ORANGE", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "sc035gs").supported());
  assert(!scanProfile("Rockchip RK3562 Y07 LP4 Board", "sc035gs").supported());
  assert(!scanProfile("Rockchip RK3562 Y02-1 LP4 Board", "sc035gs").supported());
  assert(!scanProfile("unknown", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "").supported());
  auto melon=scanProfile("Rockchip RK3562 MELON LP4 V10 Board","m00_b_ov7251 5-0060");
  assert(melon.id=="melon-ov7251" && melon.width==640 && melon.height==480);
  assert(melon.ov7251Startup && !melon.gatesExposure && melon.videoNode=="/dev/video12");
  assert(melon.light==ScanLight::ConfiguredGpio && melon.privateRuntime.empty());
  assert(!scanProfile("Rockchip RK3562 MELON LP4 Board","ov7251x").supported());
  assert(!scanProfile("Rockchip RK3562 MELON2 LP4 Board","ov7251").supported());
  assert(!scanProfile("Rockchip RK3562 MELON LP4 Board","sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 ORANGE LP4 Board","ov7251").supported());
  using Json=nlohmann::json;
  Json keys={{"RK3562_Melon_V0",{{"head_key_id","platform-led_control-event"},{"head_key_code","134"},
    {"home_key_id","platform-adc-keys-event"},{"home_key_code","102"}}}};
  Json leds={{"RK3562_Melon_V0",{{"ir_gpio_number","43"},{"white_gpio_number","44"}}}};
  auto configured=melonConfig(melon,keys,leds);
  assert(configured.keyCode==134 && configured.homeCode==102 && configured.gpio==43);
  auto rejected=[&](Json k,Json l) {bool failed=false;try{melonConfig(melon,k,l);}catch(...){failed=true;}assert(failed);};
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

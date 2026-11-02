#include "profile.hpp"
#include <cassert>
#include <iostream>
using namespace penchat_scan;
int main() {
  const auto orange = scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "m00_f_sc2356 5-0030");
  const auto y02 = scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "m00_f_sc035gs 5-0030");
  assert(orange.id == "orange-sc2356" && orange.width == 496 && orange.height == 360);
  assert(y02.id == "y02-sc035gs" && y02.width == 640 && y02.height == 480);
  const auto y07=scanProfile("Rockchip RK3562 Y07 LP4 V10 Board","sc2356");
  assert(y07.id=="y07-sc2356"&&y07.width==752&&y07.height==440);
  assert(y07.light==ScanLight::LedClass&&!y07.ledBrightness.empty());
  assert(y02.light==ScanLight::Gpio43&&!y02.gatesExposure);
  assert(orange.gatesExposure&&y07.gatesExposure);
  assert(scanProfile("ORANGE", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "sc035gs").supported());
  assert(!scanProfile("Rockchip RK3562 Y07 LP4 Board", "sc035gs").supported());
  assert(!scanProfile("Rockchip RK3562 Y02-1 LP4 Board", "sc035gs").supported());
  assert(!scanProfile("unknown", "sc2356").supported());
  assert(!scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "").supported());
  std::cout << "Scanner board/sensor profiles: explicit full-resolution capture and unknown/mismatch rejection passed\n";
}

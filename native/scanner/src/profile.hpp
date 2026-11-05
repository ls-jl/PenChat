#pragma once
#include <string>
#include <utility>

namespace penchat_scan {
enum class ScanLight { Gpio43, LedClass };
struct CaptureProfile {
  CaptureProfile() = default;
  CaptureProfile(std::string name, std::string sensorName, int w, int h)
      : id(std::move(name)), sensor(std::move(sensorName)), width(w), height(h) {}
  std::string id, sensor;
  int width = 0, height = 0;
  std::string videoNode = "/dev/video23", sensorNode = "/dev/v4l-subdev2";
  std::string keyNode = "/dev/input/by-path/led_control";
  unsigned keyCode = 134;
  ScanLight light = ScanLight::Gpio43;
  std::string ledBrightness, ledOnValue;
  std::string privateRuntime;
  bool gatesExposure = true;
  bool supported() const { return !id.empty(); }
};
// Match a known board AND its actual pen sensor. Display geometry is unrelated.
inline CaptureProfile scanProfile(const std::string &model,
                                  const std::string &sensor) {
  if (model.find("ORANGE") != std::string::npos &&
      sensor.find("sc2356") != std::string::npos)
    return {"orange-sc2356", "sc2356", 496, 360};
  if (model.find("RK3562 Y02 ") != std::string::npos &&
      sensor.find("sc035gs") != std::string::npos)
    { CaptureProfile profile{"y02-sc035gs", "sc035gs", 640, 480};
      profile.gatesExposure=false;return profile; }
  if (model.find("RK3562 Y07 ") != std::string::npos && sensor.find("sc2356") != std::string::npos)
    { CaptureProfile profile{"y07-sc2356", "sc2356", 752, 440};
      profile.light=ScanLight::LedClass;
      profile.ledBrightness="/sys/class/leds/flash-led-gpio/brightness";
      // Y07's vendor driver treats brightness as a mode, not a duty cycle:
      // stock len_onoff.sh uses 1 for white, 127 for IR and 0 for off.
      profile.ledOnValue="1";
      profile.privateRuntime="librknnrt-y07.so";
      return profile; }
  return {};
}
} // namespace penchat_scan

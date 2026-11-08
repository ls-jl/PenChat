#pragma once
#include <string>
#include <utility>
#include <stdexcept>

namespace penchat_scan {
enum class ScanLight { Gpio43, LedClass, ConfiguredGpio };
struct SensorRegion {int left=0,top=0,width=0,height=0;};
struct CaptureProfile {
  CaptureProfile() = default;
  CaptureProfile(std::string name, std::string sensorName, int w, int h)
      : id(std::move(name)), sensor(std::move(sensorName)), width(w), height(h) {}
  std::string id, sensor;
  int width = 0, height = 0;
  // Native sensor coordinates. Zero extent means the complete sensor field.
  // A physical aperture/pen housing is not valid paper for registration or paint.
  SensorRegion opticalRegion;
  std::string videoNode = "/dev/video23", sensorNode = "/dev/v4l-subdev2";
  std::string keyNode = "/dev/input/by-path/led_control";
  unsigned keyCode = 134;
  ScanLight light = ScanLight::Gpio43;
  std::string ledBrightness, ledOnValue;
  std::string privateRuntime;
  bool cvi = false;
  bool gatesExposure = true;
  bool ov7251Startup = false;
  unsigned gpio = 0;
  std::string homeName;
  unsigned homeCode = 0;
  bool supported() const { return !id.empty(); }
};
inline SensorRegion sensorRegion(const CaptureProfile &profile) {
  auto r=profile.opticalRegion;
  if(!r.width&&!r.height)r={0,0,profile.width,profile.height};
  if(r.left<0||r.top<0||r.width<2||r.height<2||
      r.width>profile.width||r.height>profile.height||
      r.left>profile.width-r.width||r.top>profile.height-r.height)
    throw std::runtime_error("invalid scan optical region");
  return r;
}
// Match a known board AND its actual pen sensor. Display geometry is unrelated.
inline CaptureProfile scanProfile(const std::string &model,
                                  const std::string &sensor) {
  if(model.find("Cvitek Cv1826 for Youdao Plum V0 version")!=std::string::npos &&
      sensor.find("PIXART_PAG7920_MIPI_240P_100FPS_10BIT")!=std::string::npos) {
    CaptureProfile p{"s6-pag7920","pag7920",320,240};p.cvi=true;p.gatesExposure=false;
    // Verified against native paper frames: left/right white housing and lower
    // dark aperture must never enter the model or overwrite the paper canvas.
    p.opticalRegion={112,8,160,192};
    p.videoNode.clear();p.sensorNode.clear();p.light=ScanLight::ConfiguredGpio;return p;
  }
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
  const auto ov=sensor.find("ov7251");
  const bool ov7251=ov!=std::string::npos && (ov==0 || sensor[ov-1]=='_') &&
    (ov+6==sensor.size() || sensor[ov+6]==' ');
  if (model.find("RK3562 MELON ") != std::string::npos && ov7251) {
    CaptureProfile profile{"melon-ov7251", "ov7251", 640, 480};
    profile.videoNode="/dev/video12";profile.gatesExposure=false;
    profile.ov7251Startup=true;profile.light=ScanLight::ConfiguredGpio;
    return profile;
  }
  return {};
}
} // namespace penchat_scan

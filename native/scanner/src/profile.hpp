#pragma once
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace penchat_scan {
// C++14 descriptions shared with UI/launcher; never contain live device handles.
enum class PlatformKind { Rockchip, Cvi };
enum class StartupPolicy { DoubleQuick, SingleQuickStable, StandardFirst, CviPipeline };
enum class ScanBoundary { HardwareGate, TimestampWindow };
enum class NodePolicy { Fixed, RkIspPaths };
enum class ScanLight { Gpio, LedClass, ConfiguredGpio };
enum class CaptureFormat { Nv12, CviGray8 };
enum class GpioOwnership { Retain, RestoreExisting, TrackExport };
enum class KeyDiscovery { Fixed, NameAndCode };
enum class OpticalRotation { None, CounterClockwise90 };
enum class CalibrationKind { SpatialMap, ConstantFile, Neutral };
enum class RuntimeKind { SystemRknn, PrivateRknn, Cvi };
enum class RuntimeCompatibility { None, Y02Dma091 };
struct SensorRegion { int left=0, top=0, width=0, height=0; };
struct CapturePolicy {
  StartupPolicy startup = StartupPolicy::DoubleQuick;
  ScanBoundary boundary = ScanBoundary::HardwareGate;
  NodePolicy nodes = NodePolicy::Fixed;
  std::vector<std::pair<std::string,std::string>> candidates;
  CaptureFormat format = CaptureFormat::Nv12;
};
struct OpticalSpec {
  OpticalRotation rotation = OpticalRotation::CounterClockwise90;
  CalibrationKind calibration = CalibrationKind::SpatialMap;
  std::string metadata = "demo_gain.json", gain = "demo_gain_160x288.f32", version;
};
struct InferenceSpec {
  RuntimeKind runtime = RuntimeKind::SystemRknn;
  RuntimeCompatibility compatibility = RuntimeCompatibility::None;
  std::string model = "pintu_32.rknn", privateLibrary;
};
struct HandoffCapabilities {
  bool nativeOcrKeys = false, mediaHandoff = false, blockingStandbyRead = false;
};
struct BoardIoSpec {
  // Firmware supplies only these named values; empty section uses fixed values.
  std::string section, lampField, otherLampField;
  std::vector<std::string> headNames, homeNames;
  int expectedHeadCode = -1, expectedHomeCode = -1;
  GpioOwnership gpioOwnership = GpioOwnership::Retain;
  KeyDiscovery keyDiscovery = KeyDiscovery::Fixed;
};
struct CaptureProfile {
  CaptureProfile() = default;
  CaptureProfile(std::string name, std::string sensorName, int w, int h)
      : id(std::move(name)), sensor(std::move(sensorName)), width(w), height(h) {}
  std::string id, sensor;
  int width = 0, height = 0;
  PlatformKind platform = PlatformKind::Rockchip;
  CapturePolicy capture;
  OpticalSpec optics;
  InferenceSpec inference;
  HandoffCapabilities handoff;
  BoardIoSpec boardIo;
  // Native coordinates; zero extent means the complete sensor field.
  SensorRegion opticalRegion;
  std::string videoNode = "/dev/video23", sensorNode = "/dev/v4l-subdev2";
  std::string keyNode = "/dev/input/by-path/led_control";
  unsigned keyCode = 134;
  ScanLight light = ScanLight::Gpio;
  std::string ledBrightness, ledOnValue="1", ledOffValue="0";
  unsigned gpio = 43;
  std::string homeName;
  unsigned homeCode = 0;
  bool supported() const { return !id.empty(); }
  bool gatesExposure() const { return capture.boundary == ScanBoundary::HardwareGate; }
};
inline SensorRegion sensorRegion(const CaptureProfile &p) {
  auto r=p.opticalRegion;
  if (!r.width && !r.height) r={0,0,p.width,p.height};
  if (r.left<0 || r.top<0 || r.width<2 || r.height<2 ||
      r.width>p.width || r.height>p.height ||
      r.left>p.width-r.width || r.top>p.height-r.height)
    throw std::runtime_error("invalid scan optical region");
  return r;
}
inline void validateProfile(const CaptureProfile &p) {
  if (!p.supported() || p.sensor.empty() || p.width<2 || p.height<2 ||
      p.keyCode>767 || p.inference.model.empty())
    throw std::runtime_error("invalid scan profile");
  sensorRegion(p);
  const bool cvi=p.platform==PlatformKind::Cvi;
  if (cvi != (p.capture.startup==StartupPolicy::CviPipeline) ||
      cvi != (p.inference.runtime==RuntimeKind::Cvi) ||
      cvi != (p.capture.format==CaptureFormat::CviGray8) ||
      (cvi && (p.width!=320 || p.height!=240 || p.gatesExposure())) ||
      (!cvi && (p.videoNode.empty() || p.sensorNode.empty())) ||
      (p.capture.nodes==NodePolicy::RkIspPaths && p.capture.candidates.empty()) ||
      (p.gatesExposure() && p.capture.startup!=StartupPolicy::DoubleQuick) ||
      (p.inference.runtime==RuntimeKind::PrivateRknn && p.inference.privateLibrary.empty()) ||
      (p.light==ScanLight::LedClass && (p.ledBrightness.empty() || p.ledOnValue.empty())) ||
      (p.optics.calibration!=CalibrationKind::Neutral && p.optics.metadata.empty()) ||
      (p.optics.calibration==CalibrationKind::SpatialMap && p.optics.gain.empty()))
    throw std::runtime_error("incompatible scan profile capabilities");
}
inline const char *startupName(StartupPolicy p) {
  switch (p) {
  case StartupPolicy::DoubleQuick: return "double_quick";
  case StartupPolicy::SingleQuickStable: return "single_quick_stable";
  case StartupPolicy::StandardFirst: return "standard_first";
  case StartupPolicy::CviPipeline: return "cvi_pipeline";
  }
  return "unknown";
}
inline std::vector<std::string> requiredScanResources(const CaptureProfile &p) {
  std::vector<std::string> files{p.inference.model};
  if (p.optics.calibration!=CalibrationKind::Neutral) files.push_back(p.optics.metadata);
  if (p.optics.calibration==CalibrationKind::SpatialMap) files.push_back(p.optics.gain);
  if (p.inference.runtime==RuntimeKind::PrivateRknn) files.push_back("lib/"+p.inference.privateLibrary);
  return files;
}
struct ProfileRegistration {
  std::string board, sensor;
  bool sensorToken = false;
  CaptureProfile profile;
};
inline const std::array<ProfileRegistration,5> &scanProfiles() {
  static const auto profiles=[] {
    CaptureProfile orange{"orange-sc2356","sc2356",496,360};
    CaptureProfile y02{"y02-sc035gs","sc035gs",640,480};
    y02.capture.startup=StartupPolicy::SingleQuickStable;
    y02.capture.boundary=ScanBoundary::TimestampWindow;
    y02.optics={OpticalRotation::CounterClockwise90,CalibrationKind::Neutral,"","","y02-sc035gs-native-neutral-v1"};
    y02.inference.compatibility=RuntimeCompatibility::Y02Dma091;
    CaptureProfile y07{"y07-sc2356","sc2356",752,440};
    y07.light=ScanLight::LedClass;
    y07.ledBrightness="/sys/class/leds/flash-led-gpio/brightness";
    y07.ledOnValue="1"; // Vendor mode: white=1, IR=127, off=0.
    y07.optics={OpticalRotation::CounterClockwise90,CalibrationKind::ConstantFile,"y07-sc2356.json","",""};
    y07.inference.runtime=RuntimeKind::PrivateRknn;
    y07.inference.privateLibrary="librknnrt-y07.so";
    y07.handoff.nativeOcrKeys=true;
    CaptureProfile melon{"melon-ov7251","ov7251",640,480};
    melon.videoNode="/dev/video12";
    melon.capture={StartupPolicy::StandardFirst,ScanBoundary::TimestampWindow,NodePolicy::RkIspPaths};
    melon.capture.candidates={{"/dev/video12","rkisp_selfpath"},{"/dev/video11","rkisp_mainpath"}};
    melon.light=ScanLight::ConfiguredGpio;
    melon.boardIo={"RK3562_Melon_V0","ir_gpio_number","white_gpio_number",
      {"led_control","platform-led_control-event"},{"adc-keys","platform-adc-keys-event"},
      134,102,GpioOwnership::TrackExport,KeyDiscovery::NameAndCode};
    melon.homeName="adc-keys";
    melon.optics={OpticalRotation::CounterClockwise90,CalibrationKind::Neutral,"","","melon-ov7251-native-neutral-v1"};
    CaptureProfile s6{"s6-pag7920","pag7920",320,240};
    s6.platform=PlatformKind::Cvi;
    s6.capture={StartupPolicy::CviPipeline,ScanBoundary::TimestampWindow,NodePolicy::Fixed};
    s6.capture.format=CaptureFormat::CviGray8;
    s6.opticalRegion={112,8,160,192};
    s6.videoNode.clear(); s6.sensorNode.clear(); s6.light=ScanLight::ConfiguredGpio;
    s6.boardIo={"Plum_V0","white_gpio_number","ir_gpio_number",
      {"platform-led_control-event"},{},-1,-1,GpioOwnership::RestoreExisting,KeyDiscovery::NameAndCode};
    s6.homeName="gpio-keys";
    s6.optics={OpticalRotation::None,CalibrationKind::Neutral,"","","s6-pag7920-optical-region-neutral-v4"};
    s6.inference={RuntimeKind::Cvi,RuntimeCompatibility::None,"pintu_rgb32_int8.cvimodel",""};
    s6.handoff={false,true,true};
    return std::array<ProfileRegistration,5>{{
      {"ORANGE","sc2356",false,orange}, {"RK3562 Y02 ","sc035gs",false,y02},
      {"RK3562 Y07 ","sc2356",false,y07}, {"RK3562 MELON ","ov7251",true,melon},
      {"Cvitek Cv1826 for Youdao Plum V0 version","PIXART_PAG7920_MIPI_240P_100FPS_10BIT",false,s6}}};
  }();
  return profiles;
}
inline CaptureProfile scanProfileById(const std::string &id) {
  for (const auto &entry:scanProfiles()) if (entry.profile.id==id) return entry.profile;
  return {};
}
// Only the registry matches identities. Public flows use resolved capabilities.
inline CaptureProfile scanProfile(const std::string &model, const std::string &sensor) {
  CaptureProfile found;
  for (const auto &entry:scanProfiles()) {
    if (model.find(entry.board)==std::string::npos) continue;
    const auto pos=sensor.find(entry.sensor);
    if (pos==std::string::npos) continue;
    if (entry.sensorToken && !((pos==0 || sensor[pos-1]=='_') &&
        (pos+entry.sensor.size()==sensor.size() || sensor[pos+entry.sensor.size()]==' '))) continue;
    if (found.supported()) throw std::runtime_error("ambiguous scan board or sensor");
    found=entry.profile;
  }
  if (found.supported()) validateProfile(found);
  return found;
}
} // namespace penchat_scan

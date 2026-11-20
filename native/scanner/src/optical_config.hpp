#pragma once
#include "profile.hpp"
#include <pen_stream/stream.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>

namespace penchat_scan {
inline pen::stream::Config opticalConfig(const CaptureProfile &p, const std::string &resources,
                                         bool diagnostic=false) {
  using namespace pen::stream;
  validateProfile(p);
  const auto region=sensorRegion(p);
  Config config;
  config.width=region.width; config.height=region.height;
  config.rotation=p.optics.rotation==OpticalRotation::None?Rotation::None:Rotation::CounterClockwise90;
  config.diagnosticAcceptAll=diagnostic;
  auto k=std::make_shared<Calibration>();
  k->rotation=config.rotation;
  k->sensorWidth=config.width; k->sensorHeight=config.height;
  if (p.optics.calibration==CalibrationKind::Neutral) {
    k->version=p.optics.version; k->gain.fill(1.f);
  } else {
    std::ifstream source(resources+"/"+p.optics.metadata);
    nlohmann::json j; source>>j;
    const auto rotation=p.optics.rotation==OpticalRotation::None?"none":"counterclockwise90";
    if (j.value("sensor_width",0)!=p.width || j.value("sensor_height",0)!=p.height ||
        j.value("rotation",std::string())!=rotation)
      throw std::runtime_error("scan calibration profile mismatch");
    k->sensorWidth=p.width; k->sensorHeight=p.height;
    k->version=j.at("version").get<std::string>();
    if (p.optics.calibration==CalibrationKind::ConstantFile) {
      if (j.value("profile",std::string())!=p.id)
        throw std::runtime_error("scan calibration identity mismatch");
      k->gain.fill(j.at("gain_constant").get<float>());
    } else {
      if (j.value("gain_shape",nlohmann::json::array())!=nlohmann::json::array({288,160}) ||
          j.value("gain_dtype",std::string())!="float32 little endian")
        throw std::runtime_error("scan calibration shape mismatch");
      std::ifstream gain(resources+"/"+p.optics.gain,std::ios::binary);
      gain.read(reinterpret_cast<char *>(k->gain.data()),k->gain.size()*sizeof(float));
      if (!gain || gain.peek()!=EOF) throw std::runtime_error("scan calibration length mismatch");
    }
    for (float value:k->gain)
      if (!std::isfinite(value) || value<.8f || value>1.4f)
        throw std::runtime_error("invalid scan calibration value");
  }
  config.calibration=std::move(k);
  return config;
}
} // namespace penchat_scan

#pragma once
#include <string>

namespace penchat_scan {
struct CaptureProfile {
  std::string id, sensor;
  int width = 0, height = 0;
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
    return {"y02-sc035gs", "sc035gs", 640, 480};
  return {};
}
} // namespace penchat_scan

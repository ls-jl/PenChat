#pragma once
namespace camera {
// Compile-time platform capabilities, consumed by shared native services.
// UI/business components never select hardware by screen resolution or model.
struct DeviceCapabilities {
  const char *family;
  bool physicalScreenConfig,sharedCaptureDisplay,photoCamera,multiLineOcr;
};
inline DeviceCapabilities deviceCapabilities() {
#ifdef PENCHAT_CVI_PLATFORM
  return {"cvi-arm32",true,true,false,false};
#else
  return {"rockchip",false,false,true,true};
#endif
}
}

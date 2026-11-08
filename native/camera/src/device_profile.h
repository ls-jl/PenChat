#pragma once
namespace camera {
// Compile-time platform capabilities, consumed by shared native services.
// UI/business components never select hardware by screen resolution or model.
struct DeviceCapabilities {
  const char *family;
  const char *requiredBoard;
  bool physicalScreenConfig,sharedCaptureDisplay,photoCamera;
};
inline DeviceCapabilities deviceCapabilities() {
#ifdef PENCHAT_CVI_PLATFORM
  return {"s6-cv1826","Cvitek Cv1826 for Youdao Plum V0 version",true,true,false};
#else
  return {"rockchip","",false,false,true};
#endif
}
}

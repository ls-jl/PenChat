#pragma once
#include "capture_stream.hpp"
#include "protocol.hpp"
#include <memory>

namespace penchat_scan {
// Frozen after firmware values and the snapshot have been validated. A new
// lease gets a new descriptor; no per-frame copies or references to temporaries.
struct ResolvedDeviceProfile {
  const CaptureProfile profile;
  const std::vector<std::string> videoNodes;
  ResolvedDeviceProfile(CaptureProfile p, std::vector<std::string> nodes)
      : profile(std::move(p)), videoNodes(std::move(nodes)) {}
};
class ScanPlatform {
public:
  virtual ~ScanPlatform()=default;
  virtual void reserve(const Json &lease)=0;
  virtual void start()=0;
  virtual bool readFrame(const FrameConsumer &)=0;
  virtual bool keyEvent(bool &, std::int64_t *timestamp=nullptr)=0;
  virtual void light(bool)=0;
  virtual void exposure(bool)=0;
  virtual void cameraToOcr()=0;
  virtual void beforeRestore(std::function<void()>)=0;
  virtual void close() noexcept=0;
};
std::unique_ptr<ScanPlatform> makeScanPlatform();
std::shared_ptr<CaptureStream> openRkCapture(const ResolvedDeviceProfile &);
} // namespace penchat_scan

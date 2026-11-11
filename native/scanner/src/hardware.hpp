#pragma once
#include "scan_platform.hpp"
#include <memory>
#include <string>

namespace penchat_scan {
CaptureProfile detectedScanProfile();
Json scanLeaseSnapshot();
int acquireScanProcessLock(unsigned waitMs = 0);
bool validScanProcessLock(int fd);
void restoreScanLease(const Json &snapshot) noexcept;
bool scanLeaseRestored(const Json &snapshot) noexcept;
std::unique_ptr<const ResolvedDeviceProfile> resolveScanProfile(const Json &lease);
void setScanLight(const CaptureProfile &, const std::string &exportMarker, bool on);
// Common lease/key/light implementation composed with one platform capture.
// Its implementation is linked only into the scanner and diagnostic probes.
class Hardware final : public ScanPlatform {
  std::shared_ptr<CaptureStream> capture_;
  std::shared_ptr<const ResolvedDeviceProfile> resolved_;
  std::function<void()> beforeRestore_;
  int key_ = -1;
  bool pressed_ = false, waitRelease_ = false, keyMonotonic_ = false;
  Json lease_;
  const CaptureProfile &profile() const { return resolved_->profile; }
  void openCamera();
  void resetKey();
public:
  ~Hardware() override { close(); }
  void open(const Json &lease);
  void beforeRestore(std::function<void()> hook) override { beforeRestore_=std::move(hook); }
  void reserve(const Json &lease) override;
  void start() override;
  void close() noexcept override;
  void cameraToOcr() override;
  bool keyEvent(bool &down, int64_t *timestamp=nullptr) override;
  bool readFrame(const FrameConsumer &) override;
  bool readFrame(const LegacyFrameConsumer &consume) {
    return readFrame(FrameConsumer([&](const FrameView &f){consume(f.y,f.stride,f.sequence,f.timestamp);}));
  }
  void light(bool) override;
  void exposure(bool) override;
};
} // namespace penchat_scan

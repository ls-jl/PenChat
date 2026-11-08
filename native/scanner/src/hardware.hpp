#pragma once
#include "protocol.hpp"
#include "profile.hpp"
#include "cvi_capture.hpp"
#include <string>
#include <vector>

namespace penchat_scan {
CaptureProfile detectedScanProfile();
Json scanLeaseSnapshot();
int acquireScanProcessLock(unsigned waitMs = 0);
bool validScanProcessLock(int fd);
void restoreScanLease(const Json &snapshot) noexcept;
bool scanLeaseRestored(const Json &snapshot) noexcept;
void setScanLight(bool on);
class Hardware {
  struct Buffer {
    void *bytes = nullptr;
    size_t size = 0;
  };
  std::shared_ptr<CaptureStream> cvi_;
  std::function<void()> beforeRestore_;
  int video_ = -1, sensor_ = -1, key_ = -1;
  int stride_ = 0;
  CaptureProfile profile_;
  bool stream_ = false, pressed_ = false, waitRelease_ = false, keyMonotonic_ = false;
  Json lease_;
  std::vector<Buffer> buffers_;
  void openCamera();
  void resetKey();

public:
  ~Hardware() { close(); }
  // Snapshot must already be retained by the parent before acquiring.
  void open(const Json &lease);
  void beforeRestore(std::function<void()> hook){beforeRestore_=std::move(hook);}
  // Reserve guardian/key ownership without starting sensor or V4L2 buffers.
  void reserve(const Json &lease);
  void start();
  void close() noexcept;
  bool keyEvent(bool &down, int64_t *timestamp = nullptr);
  bool readFrame(const std::function<void(const uint8_t *, int, uint64_t,
                                          int64_t)> &consume);
  void light(bool on) { setScanLight(on); }
  // SC2356 gates exposure; SC035GS/OV7251 free-run and the worker bounds timestamps.
  void exposure(bool enabled);
};
} // namespace penchat_scan

#pragma once
#include "protocol.hpp"
#include <string>
#include <vector>

namespace penchat_scan {
Json scanLeaseSnapshot();
int acquireScanProcessLock(unsigned waitMs = 0);
bool validScanProcessLock(int fd);
void restoreScanLease(const Json &snapshot) noexcept;
void setScanLight(bool on);
class Hardware {
  struct Buffer {
    void *bytes = nullptr;
    size_t size = 0;
  };
  int video_ = -1, sensor_ = -1, key_ = -1;
  int stride_ = 0;
  bool stream_ = false, pressed_ = false, waitRelease_ = false;
  Json lease_;
  std::vector<Buffer> buffers_;
  void openCamera();

public:
  ~Hardware() { close(); }
  // Snapshot must already be retained by the parent before acquiring.
  void open(const Json &lease);
  void close() noexcept;
  bool keyEvent(bool &down);
  bool readFrame(const std::function<void(const uint8_t *, int, uint64_t,
                                          int64_t)> &consume);
  void light(bool on) { setScanLight(on); }
  void exposure(bool enabled);
};
} // namespace penchat_scan

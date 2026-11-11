#include "hardware.hpp"
#include "hardware_ops.hpp"
#include "capture_timing.hpp"
#ifdef PENCHAT_CVI_CAPTURE
#include "cvi_capture.hpp"
#endif
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <linux/input.h>
#include <thread>
#include <unistd.h>
namespace penchat_scan {
using namespace hardware_detail;
void Hardware::open(const Json &lease) {
  reserve(lease);
  start();
}
void Hardware::reserve(const Json &lease) {
  close();
  lease_ = lease;
  try {
    resolved_=resolveScanProfile(lease_);
#ifdef PENCHAT_CVI_CAPTURE
    if(profile().platform==PlatformKind::Cvi)verifyCviCaptureRuntime();
#endif
    for (auto &p : lease_.at("guardians"))
      if (!p["paused"].get<bool>() && same(p) &&
          match(p["pid"], "guardian_run", "/usr/bin/runCapFrame"))
        require(kill(p["pid"].get<int>(), SIGSTOP) == 0,
                "cannot acquire scan owner");
    for (auto &p : lease_.at("captures"))
      if (same(p) && match(p["pid"], "CaptureFrame", "CaptureFrame"))
        kill(p["pid"].get<int>(), SIGTERM);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    for (auto &p : lease_.at("captures"))
      if (same(p) && match(p["pid"], "CaptureFrame", "CaptureFrame"))
        kill(p["pid"].get<int>(), SIGKILL);
    // SIGKILL is asynchronous. Do not request our V4L2 buffers while the old
    // owner is still closing the same video node in the kernel.
    const auto stopped = std::chrono::steady_clock::now() +
                         std::chrono::seconds(2);
    for (;;) {
      bool alive = false;
      for (auto &p : lease_.at("captures"))
        alive = alive ||
                (same(p) && match(p["pid"], "CaptureFrame", "CaptureFrame"));
      if (!alive)
        break;
      require(std::chrono::steady_clock::now() < stopped,
              "system capture did not release video node");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    light(false);
    key_ = ::open(profile().keyNode.c_str(),
                  O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    require(key_ >= 0, "scan key unavailable");
    int keyClock = CLOCK_MONOTONIC;
    keyMonotonic_ = io(key_, EVIOCSCLOCKID, &keyClock) == 0;
    char name[128]{};
    require(io(key_, EVIOCGNAME(sizeof(name)), name) >= 0 &&
                std::string(name) == "led_control",
            "wrong scan key");
    int one = 1;
    require(io(key_, EVIOCGRAB, &one) == 0, "scan key busy");
    resetKey();
  } catch (...) {
    close();
    throw;
  }
}
void Hardware::resetKey() {
  input_event event{};
  while (::read(key_, &event, sizeof(event)) == sizeof(event)) {}
  require(errno == EAGAIN || errno == EWOULDBLOCK, "cannot drain scan key");
  unsigned long keys[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))]{};
  require(io(key_, EVIOCGKEY(sizeof(keys)), keys) >= 0, "cannot read scan key");
  pressed_ = false;
  const unsigned code = profile().keyCode;
  waitRelease_ = (keys[code / (8 * sizeof(long))] &
                  (1UL << (code % (8 * sizeof(long))))) != 0;
}
void Hardware::start() {
  try {
    require(key_ >= 0 && !capture_ && !lease_.empty(), "scan hardware not reserved");
    openCamera();
    // Presses made while preparing cannot become a delayed, empty scan.
    resetKey();
  } catch (...) {
    close();
    throw;
  }
}
void Hardware::openCamera() {
  require(bool(resolved_),"scan profile not resolved");
#ifdef PENCHAT_CVI_CAPTURE
  require(profile().platform==PlatformKind::Cvi,"RK capture unavailable in CVI executable");
  capture_=openCviCapture();
#else
  require(profile().platform==PlatformKind::Rockchip,"CVI capture unavailable in RK executable");
  capture_=openRkCapture(*resolved_);
#endif
}
bool Hardware::keyEvent(bool &down, int64_t *timestamp) {
  input_event e{};
  while (::read(key_, &e, sizeof(e)) == sizeof(e)) {
    if (e.type == EV_SYN && e.code == SYN_DROPPED)
      throw std::runtime_error("scan key events lost");
    if (e.type != EV_KEY || e.code != profile().keyCode || (e.value != 0 && e.value != 1))
      continue;
    if (waitRelease_) {
      if (e.value == 0)
        waitRelease_ = false;
      continue;
    }
    bool next = e.value == 1;
    if (next == pressed_)
      continue;
    pressed_ = next;
    down = next;
    if (timestamp) {
      timespec mono{}, real{};
      require(clock_gettime(CLOCK_MONOTONIC, &mono) == 0 && clock_gettime(CLOCK_REALTIME, &real) == 0,
              "cannot timestamp scan key");
      *timestamp = scanKeyTimestamp(e.time.tv_sec, e.time.tv_usec, keyMonotonic_,
          int64_t(mono.tv_sec)*1000000 + mono.tv_nsec/1000,
          int64_t(real.tv_sec)*1000000 + real.tv_nsec/1000);
    }
    return true;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    throw std::runtime_error("scan key disconnected");
  return false;
}
void Hardware::exposure(bool enabled) {
  require(bool(capture_),"scan capture unavailable");capture_->exposure(enabled);
}
bool Hardware::readFrame(const FrameConsumer &consume) {
  require(bool(capture_),"scan capture unavailable");return capture_->read(consume);
}
void Hardware::light(bool on) {
  if (!resolved_ || lease_.empty()) {
    require(!on,"scan light lease unavailable");return;
  }
  setScanLight(profile(),lease_.value("gpio_export_marker",std::string()),on);
}
void Hardware::cameraToOcr() {
  require(key_ >= 0, "OCR key lease unavailable");
  resetKey();
  const int key = key_; const bool waitRelease = waitRelease_;
  const auto retained=resolved_;
  const bool monotonic=keyMonotonic_;
  key_ = -1;
  close(); // Restore camera/guardian while retaining the same evdev grab.
  key_ = key; waitRelease_ = waitRelease;
  resolved_=retained; keyMonotonic_=monotonic;
  // The launcher has already masked only its own host head-key clients. Let
  // CaptureFrame receive the physical edge too: it owns lamp/start/stop, while
  // this nonexclusive reader forwards the same edge to OCR without home UI.
  require(io(key_, EVIOCGRAB, nullptr)==0,"cannot release OCR capture key grab");
}
void Hardware::close() noexcept {
  bool safeRestore=true;
  if(!lease_.empty()&&resolved_&&profile().handoff.mediaHandoff&&beforeRestore_)try{beforeRestore_();}catch(...){safeRestore=false;}
  capture_.reset();
  if (key_ >= 0) {
    io(key_, EVIOCGRAB, nullptr);
    ::close(key_);
  }
  key_ = -1;
  if (!lease_.empty()) {
    // Parent retained the lease and will restore after its display is quiescent
    // if the child no longer has a working control channel.
    if(safeRestore)restoreScanLease(lease_);
  }
  lease_ = Json();
  resolved_.reset();
  pressed_ = waitRelease_ = keyMonotonic_ = false;
}
std::unique_ptr<ScanPlatform> makeScanPlatform() {
  return std::unique_ptr<ScanPlatform>(new Hardware);
}
} // namespace penchat_scan

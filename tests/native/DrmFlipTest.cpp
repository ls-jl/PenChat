#include <cstdint>
#include <pthread.h>
#include <string>
#include <vector>
#define private public
#include "overlay.h"
#undef private
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <poll.h>
#include <sys/syscall.h>
#include <cerrno>
#include <chrono>

namespace {
int events[2];
void* pendingContext = nullptr;
uint32_t submittedFlags = 0;
unsigned submissions = 0;
bool reject = false;
bool rejectAtomic = false;
unsigned legacySubmissions = 0;
const uint32_t crtcObject = 42, outFenceProperty = 43;
bool crtcIncluded = false;
bool rejectCrtcProperty = false;
bool rejectZpos = false;
bool zposIncluded = false;
const uint32_t zposProperty = 44;
bool interruptPoll = false;
bool interruptUntilTimeout = false;
std::vector<int> interruptedTimeouts;
}
extern "C" int poll(struct pollfd* fds, nfds_t count, int timeout) {
  if (interruptUntilTimeout) {
    interruptedTimeouts.push_back(timeout);
    // Spend real monotonic time in an interrupted syscall, as a signal does.
    usleep(25000);
    errno = EINTR;
    return -1;
  }
  if (interruptPoll) {interruptPoll = false; errno = EINTR; return -1;}
  return static_cast<int>(syscall(SYS_poll, fds, count, timeout));
}
extern "C" drmModeAtomicReqPtr drmModeAtomicAlloc() {
  crtcIncluded = false;
  zposIncluded = false;
  return reinterpret_cast<drmModeAtomicReqPtr>(1);
}
extern "C" void drmModeAtomicFree(drmModeAtomicReqPtr) {}
extern "C" int drmModeAtomicAddProperty(drmModeAtomicReqPtr, uint32_t object, uint32_t property, uint64_t value) {
  if (property == zposProperty) zposIncluded = true;
  if (object == crtcObject && property == outFenceProperty) {
    assert(value == 0); // Include CRTC state without requesting a fence or modeset.
    if (rejectCrtcProperty) return -1;
    crtcIncluded = true;
  }
  return 0;
}
extern "C" int drmModeAtomicCommit(int, drmModeAtomicReqPtr, uint32_t flags, void* context) {
  ++submissions;
  submittedFlags = flags;
  // Model the already-attached plane: setting its same CRTC_ID does not
  // add a CRTC state, so event reservation rejects a plane-only request.
  if ((flags & DRM_MODE_PAGE_FLIP_EVENT) && !crtcIncluded) {errno = EINVAL; return -1;}
  if (reject) {errno = EBUSY; return -1;}
  if (rejectAtomic || (rejectZpos && zposIncluded)) {errno = EINVAL; return -1;}
  pendingContext = context;
  return 0;
}
extern "C" int drmModeSetPlane(int, uint32_t, uint32_t, uint32_t, uint32_t,
    int32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {
  ++legacySubmissions;
  return 0;
}
extern "C" int drmHandleEvent(int fd, drmEventContextPtr context) {
  char event;
  assert(read(fd, &event, 1) == 1);
  context->page_flip_handler(fd, 0, 0, 0, pendingContext);
  pendingContext = nullptr;
  return 0;
}
static void complete() {assert(write(events[1], "f", 1) == 1);}
int main() {
  assert(pipe(events) == 0);
  camera::Overlay overlay;
  overlay.drm_ = dlopen(nullptr, RTLD_NOW);
  assert(overlay.drm_);
  overlay.fd_ = events[0];
  overlay.planeId_ = 1;
  overlay.crtcId_ = crtcObject;
  overlay.propOutFence_ = outFenceProperty;
  overlay.atomic_ = true;
  overlay.crtcW_ = overlay.crtcH_ = 2;
  overlay.hole_ = {0, 0, 2, 2};
  overlay.propFb_ = overlay.propCrtc_ = overlay.propSrcX_ = overlay.propSrcY_ = 1;
  overlay.propSrcW_ = overlay.propSrcH_ = overlay.propCrtcX_ = overlay.propCrtcY_ = 1;
  overlay.propCrtcW_ = overlay.propCrtcH_ = 1;
  std::vector<uint8_t> first(16, 0x11), second(16, 0x22), frame(16, 0x33);
  for (int i = 0; i < 2; ++i) {
    overlay.bufs_[i].fbId = i + 1;
    overlay.bufs_[i].pitch = 8;
    overlay.bufs_[i].height = 2;
    overlay.bufs_[i].size = 16;
  }
  overlay.bufs_[0].map = first.data();
  overlay.bufs_[1].map = second.data();
  rejectCrtcProperty = true;
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(submissions == 0 && overlay.cur_ == 0 && !overlay.flipPending_);
  assert(second == std::vector<uint8_t>(16, 0x22));
  rejectCrtcProperty = false;
  assert(overlay.presentBgra(frame.data(), 2, 2));
  assert(first == frame && overlay.cur_ == 1);
  assert(submittedFlags == (DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT));
  interruptPoll = true;
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(!overlay.completionFailed_ && overlay.lastError().empty());
  const auto untouched = second;
  frame.assign(16, 0x44);
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(second == untouched && submissions == 1 && overlay.lastError().empty());
  complete();
  assert(overlay.presentBgra(frame.data(), 2, 2));
  assert(second == frame && overlay.cur_ == 0);
  const auto scanout = first;
  frame.assign(16, 0x55);
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(first == scanout && submissions == 2 && overlay.flipPending_);
  rejectAtomic = true;
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(overlay.flipPending_ && first == scanout && submissions == 2 && legacySubmissions == 0);
  rejectAtomic = false;
  complete();
  reject = true;
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(!overlay.flipPending_ && overlay.cur_ == 0);
  reject = false;
  rejectAtomic = true;
  const auto failedSubmissions = submissions;
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(submissions == failedSubmissions + 2 && legacySubmissions == 0);
  assert(!overlay.flipPending_ && overlay.cur_ == 0);
  assert(overlay.lastError() == "DRM atomic submission failed: " + std::to_string(EINVAL));
  rejectAtomic = false;
  assert(overlay.presentBgra(frame.data(), 2, 2));
  assert(first == frame && overlay.cur_ == 1);
  complete();
  interruptPoll = true;
  assert(overlay.finishPendingFlip(true));
  assert(!interruptPoll && !overlay.flipPending_ && !overlay.completionFailed_);
  // A rejected zpos request still needs explicit CRTC state in its retry.
  overlay.propZpos_ = zposProperty;
  rejectZpos = true;
  const auto beforeZposRetry = submissions;
  assert(overlay.presentBgra(frame.data(), 2, 2));
  assert(submissions == beforeZposRetry + 2 && overlay.flipPending_);
  complete();
  assert(overlay.finishPendingFlip(true));
  rejectZpos = false;
  overlay.propZpos_ = 0;
  // An unavailable binding must fail before submission or buffer rotation.
  overlay.propOutFence_ = 0;
  const auto beforeMissingBinding = submissions;
  const auto beforeMissingCur = overlay.cur_;
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(submissions == beforeMissingBinding && overlay.cur_ == beforeMissingCur);
  assert(!overlay.flipPending_ && overlay.lastError() == "DRM CRTC event binding unavailable");
  overlay.propOutFence_ = outFenceProperty;
  overlay.error_.clear();
  assert(overlay.commit(0, 0, 0, 0, 0, 1, 1));
  assert(submittedFlags == 0 && !overlay.flipPending_);
  assert(overlay.presentBgra(frame.data(), 2, 2));
  const auto heldFirst = first, heldSecond = second;
  const auto heldSubmissions = submissions;
  interruptUntilTimeout = true;
  const auto waitStart = std::chrono::steady_clock::now();
  overlay.disable(); // Repeated signals must not restart the lifecycle budget.
  const auto waited = std::chrono::steady_clock::now() - waitStart;
  interruptUntilTimeout = false;
  assert(interruptedTimeouts.size() >= 2);
  assert(interruptedTimeouts.front() == 100);
  assert(interruptedTimeouts.back() < interruptedTimeouts.front());
  assert(waited >= std::chrono::milliseconds(90));
  assert(waited < std::chrono::milliseconds(500));
  assert(overlay.completionFailed_ && overlay.flipPending_);
  assert(overlay.lastError() == "DRM flip completion timed out");
  assert(submissions == heldSubmissions);
  frame.assign(16, 0x66);
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(first == heldFirst && second == heldSecond);
  assert(overlay.lastError() == "DRM flip completion timed out");
  // Independently inject a disconnected event fd. A fatal poll result also
  // quarantines both buffers and is retained across subsequent presents.
  overlay.completionFailed_ = false;
  overlay.error_.clear();
  close(events[1]);
  assert(!overlay.presentBgra(frame.data(), 2, 2));
  assert(overlay.completionFailed_ && overlay.lastError() == "DRM completion poll failed");
  assert(first == heldFirst && second == heldSecond && submissions == heldSubmissions);
  // The test owns these CPU mappings and pipe; suppress DRM teardown only.
  memset(overlay.bufs_, 0, sizeof(overlay.bufs_));
  overlay.fd_ = -1;
  overlay.flipPending_ = false;
  overlay.completionFailed_ = false;
  close(events[0]);
}

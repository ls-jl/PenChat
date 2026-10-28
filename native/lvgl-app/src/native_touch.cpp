#include "native_touch.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>
namespace penchat {
void TouchFrames::reset(const std::vector<Slot> &slots, int selected, bool wait) {
  slots_ = slots;
  selected_ = selected;
  previous_ = Json::array();
  dropped_ = false;
  waitClear_ = wait || std::any_of(slots.begin(), slots.end(), [](const Slot &s) { return s.id >= 0; });
}
Json TouchFrames::packet(const Json &points, bool cancelled) {
  if (!points.empty()) {
    lastX_ = points[0]["x"].get<int>();
    lastY_ = points[0]["y"].get<int>();
  }
  return {{"type", "touch"}, {"source", "native-evdev"}, {"gesture", serial_},
          {"pressed", !points.empty() && !cancelled}, {"cancelled", cancelled},
          {"x", lastX_}, {"y", lastY_}, {"points", points}};
}
Json TouchFrames::cancel() {
  previous_ = Json::array();
  waitClear_ = true;
  return packet(previous_, true);
}
std::vector<Json> TouchFrames::event(int type, int code, int value) {
  std::vector<Json> out;
  if (type == EV_SYN && code == SYN_DROPPED) {
    dropped_ = true;
    out.push_back(cancel());
    return out;
  }
  if (dropped_) return out; // Reader discards the backlog before resynchronizing.
  if (type == EV_ABS) {
    if (code == ABS_MT_SLOT) selected_ = value;
    else if (selected_ >= 0 && size_t(selected_) < slots_.size()) {
      auto &s = slots_[selected_];
      if (code == ABS_MT_TRACKING_ID) s.id = value;
      else if (code == ABS_MT_POSITION_X) s.x = value;
      else if (code == ABS_MT_POSITION_Y) s.y = value;
    }
    return out;
  }
  if (type != EV_SYN || code != SYN_REPORT) return out;
  Json points = Json::array();
  bool outside = false;
  for (size_t i = 0; i < slots_.size(); ++i) {
    const auto &s = slots_[i];
    if (s.id < 0) continue;
    const auto p = logicalTouch(screen_, s.x, s.y);
    outside |= p.first < 0 || p.second < 0 || p.first >= screen_.width || p.second >= screen_.height;
    points.push_back({{"id", std::to_string(i) + ":" + std::to_string(s.id)}, {"x", p.first}, {"y", p.second}});
  }
  if (waitClear_) {
    if (points.empty()) waitClear_ = false;
    return out;
  }
  // Reject gestures that begin on a physical bezel. An accepted pointer may
  // drift beyond the viewport: preserve its movement/release so edge keys use
  // their normal slop and image/text drags keep their existing bounds.
  if (outside && previous_.empty()) {
    waitClear_ = true;
    return out;
  }
  // A contact replacement in one frame is two taps, not a drag between keys.
  if (previous_.size() == 1 && points.size() == 1 && previous_[0]["id"] != points[0]["id"])
    out.push_back(packet(Json::array()));
  if (!points.empty() && (previous_.empty() || !out.empty())) ++serial_;
  if (!points.empty() || !previous_.empty()) out.push_back(packet(points));
  previous_ = std::move(points);
  return out;
}
void NativeTouch::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  frames_.reset({});
}
void NativeTouch::resync(bool wait) {
  input_absinfo slot{};
  if (ioctl(fd_, EVIOCGABS(ABS_MT_SLOT), &slot) < 0 || slot.maximum < 0 || slot.maximum >= 32)
    throw std::runtime_error("无法同步原生触摸设备");
  std::vector<TouchFrames::Slot> slots(slot.maximum + 1);
  std::vector<int> values(slots.size() + 1);
  for (int code : {ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y}) {
    values[0] = code;
    if (ioctl(fd_, EVIOCGMTSLOTS(values.size() * sizeof(int)), values.data()) < 0)
      throw std::runtime_error("无法读取原生触摸槽状态");
    for (size_t i = 0; i < slots.size(); ++i) {
      if (code == ABS_MT_TRACKING_ID) slots[i].id = values[i + 1];
      else if (code == ABS_MT_POSITION_X) slots[i].x = values[i + 1];
      else slots[i].y = values[i + 1];
    }
  }
  frames_.reset(slots, slot.value, wait);
}
void NativeTouch::open() {
  for (int i = 0; i < 32; ++i) {
    char path[64];
    snprintf(path, sizeof(path), "/dev/input/event%d", i);
    const int candidate = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (candidate < 0) continue;
    unsigned long props[2] = {};
    input_absinfo slot{}, x{}, y{}, id{};
    const bool direct = ioctl(candidate, EVIOCGPROP(sizeof(props)), props) >= 0 && (props[0] & (1UL << INPUT_PROP_DIRECT));
    if (direct && ioctl(candidate, EVIOCGABS(ABS_MT_SLOT), &slot) >= 0 && slot.minimum == 0 && slot.maximum >= 0 && slot.maximum < 32 &&
        ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_X), &x) >= 0 && ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_Y), &y) >= 0 &&
        ioctl(candidate, EVIOCGABS(ABS_MT_TRACKING_ID), &id) >= 0) {
      fd_ = candidate;
      device_ = path;
      resync(false);
      return;
    }
    ::close(candidate);
  }
  throw std::runtime_error("无法打开原生触摸设备，请检查设备连接或权限");
}
std::vector<Json> NativeTouch::poll(bool foreground) {
  std::vector<Json> out;
  if (!foreground || !enabled_) { close(); return out; }
  if (fd_ < 0) open();
  input_event events[64];
  for (int batch = 0; batch < 8; ++batch) {
    const auto bytes = ::read(fd_, events, sizeof(events));
    if (bytes < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // ioctl returns the current kernel state, potentially newer than every
        // frame still queued on this fd. Never replay old frames over that state.
        if (frames_.dropped()) resync(true);
        break;
      }
      close();
      throw std::runtime_error("原生触摸设备读取失败，请重新打开应用");
    }
    if (!bytes || bytes % sizeof(input_event)) {
      close();
      throw std::runtime_error("原生触摸设备已断开");
    }
    for (size_t i = 0; i < size_t(bytes) / sizeof(input_event); ++i) {
      const auto &e = events[i];
      auto packets = frames_.event(e.type, e.code, e.value);
      for (auto &p : packets) out.push_back(std::move(p));
    }
  }
  return out;
}
void NativeTouch::wait(unsigned milliseconds) const {
  if (fd_ < 0) { usleep(milliseconds * 1000); return; }
  pollfd p{fd_, POLLIN, 0};
  ::poll(&p, 1, int(milliseconds));
}
Json NativeTouch::diagnostics() const {
  return {{"source", enabled_ ? "native-evdev" : "host-test"}, {"active", active()}, {"device", device_}, {"exclusive", false}};
}
} // namespace penchat

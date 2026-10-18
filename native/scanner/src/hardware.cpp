#include "hardware.hpp"
#include "file_read.hpp"
#include "app_identity.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
namespace penchat_scan {
namespace {
std::string read(const std::string &p) {
  return readOptionalFile(p);
}
std::string startTime(int pid) {
  auto s = read("/proc/" + std::to_string(pid) + "/stat");
  auto end = s.rfind(')');
  if (end == std::string::npos)
    return {};
  std::istringstream in(s.substr(end + 2));
  std::string v;
  for (int i = 3; i <= 22; ++i)
    if (!(in >> v))
      return {};
  return v;
}
bool same(const Json &p) {
  return !p.value("start", std::string()).empty() &&
         startTime(p.at("pid").get<int>()) == p.at("start").get<std::string>();
}
bool match(int pid, const std::string &comm, const std::string &needle) {
  return read("/proc/" + std::to_string(pid) + "/comm") == comm + "\n" &&
         read("/proc/" + std::to_string(pid) + "/cmdline").find(needle) !=
             std::string::npos;
}
int io(int fd, unsigned long request, void *p) {
  int rc;
  do {
    rc = ioctl(fd, request, p);
  } while (rc < 0 && errno == EINTR);
  return rc;
}
void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
} // namespace
int acquireScanProcessLock(unsigned waitMs) {
  const char *path = "/tmp/penchat-scanner-" PENCHAT_APP_ID ".lock";
  int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0)
    throw std::runtime_error("cannot open scanner process lock");
  const auto end =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
  for (;;) {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
      return fd;
    if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
        std::chrono::steady_clock::now() >= end)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ::close(fd);
  throw std::runtime_error("another scanner process is still exiting");
}
bool validScanProcessLock(int fd) {
  struct stat opened {
  }, named{};
  return fd >= 0 && fstat(fd, &opened) == 0 &&
         stat("/tmp/penchat-scanner-" PENCHAT_APP_ID ".lock", &named) == 0 &&
         opened.st_dev == named.st_dev && opened.st_ino == named.st_ino &&
         flock(fd, LOCK_EX | LOCK_NB) == 0;
}
Json scanLeaseSnapshot() {
  require(read("/proc/device-tree/model").find("ORANGE") != std::string::npos,
          "unsupported scan board");
  Json j{{"guardians", Json::array()}, {"captures", Json::array()}};
  DIR *d = opendir("/proc");
  require(d, "cannot inspect scan owner");
  while (auto e = readdir(d)) {
    int pid = atoi(e->d_name);
    if (pid <= 1)
      continue;
    bool g = match(pid, "guardian_run", "/usr/bin/runCapFrame");
    bool c = match(pid, "CaptureFrame", "CaptureFrame");
    if (!g && !c)
      continue;
    auto stat = read("/proc/" + std::to_string(pid) + "/stat");
    auto end = stat.rfind(')');
    bool paused = end != std::string::npos && stat.size() > end + 2 &&
                  stat[end + 2] == 'T';
    j[g ? "guardians" : "captures"].push_back(
        {{"pid", pid}, {"start", startTime(pid)}, {"paused", paused}});
  }
  closedir(d);
  require(!j["guardians"].empty(), "scan owner cannot be restored");
  for (auto &p : j["guardians"])
    require(!p["paused"].get<bool>(), "system scanner already paused");
  for (auto &p : j["captures"])
    require(!p["paused"].get<bool>(), "system capture already paused");
  // runCapFrame may still be starting CaptureFrame after our previous release.
  // Taking an empty snapshot would leave that late child outside the lease.
  require(!j["captures"].empty(), "system capture preparing");
  return j;
}
void setScanLight(bool on) {
  // ORANGE's vendor len_onoff.sh maps the white scanner LED to GPIO43.
  // Configure while arming, then toggle directly without fork/shell latency.
  require(read("/proc/device-tree/model").find("ORANGE") != std::string::npos,
          "unsupported scan light board");
  auto write = [](const char *path, const char *value) {
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
      return false;
    size_t size = strlen(value);
    bool okay = ::write(fd, value, size) == ssize_t(size);
    ::close(fd);
    return okay;
  };
  const char *value = "/sys/class/gpio/gpio43/value";
  if (access(value, F_OK) != 0) {
    require(write("/sys/class/gpio/export", "43"), "cannot export scan light");
    require(write("/sys/class/gpio/gpio43/direction", "out"),
            "cannot configure scan light");
  }
  require(write(value, on ? "1" : "0"), "cannot switch scan light");
}
void restoreScanLease(const Json &j) noexcept {
  bool held = false;
  try {
    for (auto &p : j.value("guardians", Json::array())) {
      if (p.value("paused", false) || !same(p) ||
          !match(p["pid"], "guardian_run", "/usr/bin/runCapFrame"))
        continue;
      auto state =
          read("/proc/" + std::to_string(p["pid"].get<int>()) + "/stat");
      auto end = state.rfind(')');
      if (end != std::string::npos && state.size() > end + 2 &&
          state[end + 2] == 'T')
        held = true;
    }
  } catch (...) {
  }
  // A normally exited worker may already have restored the guardian. Do not
  // disturb the replacement CaptureFrame or a newly started system OCR scan.
  if (!held)
    return;
  try {
    setScanLight(false);
  } catch (...) {
  }
  try {
    if (read("/sys/class/video4linux/v4l-subdev2/name").find("sc2356") !=
        std::string::npos) {
      int fd = ::open("/dev/v4l-subdev2", O_RDWR | O_CLOEXEC);
      if (fd >= 0) {
        int zero = 0;
        io(fd, 0x400456ca, &zero);
        io(fd, 0x400456ea, &zero);
        ::close(fd);
      }
    }
  } catch (...) {
  }
  try {
    for (auto &p : j.value("guardians", Json::array()))
      if (!p.value("paused", false) && same(p) &&
          match(p["pid"], "guardian_run", "/usr/bin/runCapFrame"))
        kill(p["pid"].get<int>(), SIGCONT);
  } catch (...) {
  }
}
void Hardware::open(const Json &lease) {
  close();
  lease_ = lease;
  try {
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
    setScanLight(false);
    key_ = ::open("/dev/input/by-path/led_control",
                  O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    require(key_ >= 0, "scan key unavailable");
    char name[128]{};
    require(io(key_, EVIOCGNAME(sizeof(name)), name) >= 0 &&
                std::string(name) == "led_control",
            "wrong scan key");
    int one = 1;
    require(io(key_, EVIOCGRAB, &one) == 0, "scan key busy");
    unsigned long keys[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))]{};
    require(io(key_, EVIOCGKEY(sizeof(keys)), keys) >= 0,
            "cannot read scan key");
    pressed_ = false;
    waitRelease_ = false;
    constexpr unsigned code = 134;
    waitRelease_ = (keys[code / (8 * sizeof(long))] &
                    (1UL << (code % (8 * sizeof(long))))) != 0;
    openCamera();
  } catch (...) {
    close();
    throw;
  }
}
void Hardware::openCamera() {
  // ORANGE profile; verify the sensor before changing any streaming controls.
  auto sensorName = read("/sys/class/video4linux/v4l-subdev2/name");
  require(sensorName.find("sc2356") != std::string::npos,
          "unsupported pen sensor");
  video_ = ::open("/dev/video23", O_RDWR | O_NONBLOCK | O_CLOEXEC);
  require(video_ >= 0, "pen capture busy");
  v4l2_format f{};
  f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  f.fmt.pix_mp.width = 496;
  f.fmt.pix_mp.height = 360;
  f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
  f.fmt.pix_mp.num_planes = 1;
  require(io(video_, VIDIOC_S_FMT, &f) == 0 && f.fmt.pix_mp.width == 496 &&
              f.fmt.pix_mp.height == 360 && f.fmt.pix_mp.num_planes == 1 &&
              f.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV12,
          "unsupported pen NV12 profile");
  stride_ = f.fmt.pix_mp.plane_fmt[0].bytesperline;
  require(stride_ >= 496, "invalid pen stride");
  v4l2_requestbuffers r{};
  // Absorb short scheduler stalls without dropping real sensor frames.
  r.count = 12;
  r.type = f.type;
  r.memory = V4L2_MEMORY_MMAP;
  require(io(video_, VIDIOC_REQBUFS, &r) == 0 && r.count >= 2 && r.count <= 16,
          "pen buffers unavailable");
  buffers_.resize(r.count);
  for (unsigned n = 0; n < r.count; ++n) {
    v4l2_buffer b{};
    v4l2_plane p[VIDEO_MAX_PLANES]{};
    b.type = f.type;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = n;
    b.length = VIDEO_MAX_PLANES;
    b.m.planes = p;
    require(io(video_, VIDIOC_QUERYBUF, &b) == 0, "pen QUERYBUF failed");
    buffers_[n].size = p[0].length;
    auto ptr = mmap(nullptr, p[0].length, PROT_READ | PROT_WRITE, MAP_SHARED,
                    video_, p[0].m.mem_offset);
    require(ptr != MAP_FAILED, "pen mmap failed");
    buffers_[n].bytes = ptr;
    require(io(video_, VIDIOC_QBUF, &b) == 0, "pen QBUF failed");
  }
  sensor_ = ::open("/dev/v4l-subdev2", O_RDWR | O_CLOEXEC);
  require(sensor_ >= 0, "pen sensor unavailable");
  int one = 1;
  require(io(sensor_, 0x400456ea, &one) == 0, "pen quick-only failed");
  int type = f.type;
  require(io(video_, VIDIOC_STREAMON, &type) == 0, "pen STREAMON failed");
  stream_ = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  require(io(sensor_, 0x400456ca, &one) == 0, "pen quick stream failed");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  require(io(sensor_, 0x400456ca, &one) == 0, "pen quick stream retry failed");
}
bool Hardware::keyEvent(bool &down) {
  input_event e{};
  while (::read(key_, &e, sizeof(e)) == sizeof(e)) {
    if (e.type == EV_SYN && e.code == SYN_DROPPED)
      throw std::runtime_error("scan key events lost");
    if (e.type != EV_KEY || e.code != 134 || (e.value != 0 && e.value != 1))
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
    return true;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    throw std::runtime_error("scan key disconnected");
  return false;
}
void Hardware::exposure(bool enabled) {
  int value = enabled ? 1 : 0;
  require(sensor_ >= 0 && io(sensor_, 0x400456ca, &value) == 0,
          "cannot change pen exposure");
}
bool Hardware::readFrame(
    const std::function<void(const uint8_t *, int, uint64_t, int64_t)>
        &consume) {
  v4l2_buffer b{};
  v4l2_plane p[VIDEO_MAX_PLANES]{};
  b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  b.memory = V4L2_MEMORY_MMAP;
  b.length = VIDEO_MAX_PLANES;
  b.m.planes = p;
  if (io(video_, VIDIOC_DQBUF, &b) < 0) {
    if (errno == EAGAIN || errno == EINTR)
      return false;
    throw std::runtime_error("pen DQBUF failed");
  }
  try {
    require(b.index < buffers_.size(), "invalid pen buffer index");
    auto &v = buffers_[b.index];
    size_t used = p[0].bytesused, off = p[0].data_offset;
    require(off <= used && used <= v.size &&
                used - off >= size_t(stride_) * 360 * 3 / 2,
            "invalid pen NV12 payload");
    require(!(b.flags & V4L2_BUF_FLAG_ERROR), "pen frame error");
    consume(static_cast<const uint8_t *>(v.bytes) + off, stride_, b.sequence,
            int64_t(b.timestamp.tv_sec) * 1000000 + b.timestamp.tv_usec);
  } catch (...) {
    io(video_, VIDIOC_QBUF, &b);
    throw;
  }
  require(io(video_, VIDIOC_QBUF, &b) == 0, "pen requeue failed");
  return true;
}
void Hardware::close() noexcept {
  if (sensor_ >= 0) {
    int zero = 0;
    io(sensor_, 0x400456ca, &zero);
    io(sensor_, 0x400456ea, &zero);
  }
  if (video_ >= 0 && stream_) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    io(video_, VIDIOC_STREAMOFF, &type);
  }
  stream_ = false;
  for (auto &b : buffers_)
    if (b.bytes)
      munmap(b.bytes, b.size);
  buffers_.clear();
  if (video_ >= 0)
    ::close(video_);
  if (sensor_ >= 0)
    ::close(sensor_);
  if (key_ >= 0) {
    io(key_, EVIOCGRAB, nullptr);
    ::close(key_);
  }
  video_ = sensor_ = key_ = -1;
  if (!lease_.empty())
    restoreScanLease(lease_);
  lease_ = Json();
  pressed_ = waitRelease_ = false;
}
} // namespace penchat_scan

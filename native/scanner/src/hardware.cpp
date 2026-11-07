#include "hardware.hpp"
#include "melon_config.hpp"
#include "start_policy.hpp"
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
// Published before light requests, changed only after their owner joins/off().
std::string gpioExportMarker;
CaptureProfile lightProfile;
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
CaptureProfile configured(CaptureProfile p) {
  if(p.ov7251Startup)
    return melonConfig(p,Json::parse(read("/usr/configs/hal_key_config.json")),
                       Json::parse(read("/usr/configs/hal_led_config.json")));
  return p;
}
std::string melonKey(unsigned code) {
  std::string selected;
  auto dir=opendir("/dev/input");require(dir,"scan key unavailable");
  while(auto e=readdir(dir)) {
    if(std::string(e->d_name).find("event")!=0)continue;
    const auto path=std::string("/dev/input/")+e->d_name;
    int fd=::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
    char name[128]{};unsigned char bits[(KEY_MAX+8)/8]{};
    const bool match=io(fd,EVIOCGNAME(sizeof(name)),name)>=0 && std::string(name)=="led_control" &&
      io(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits)>=0 && (bits[code/8]&(1u<<(code%8)));
    ::close(fd);
    if(match) {if(!selected.empty()){closedir(dir);throw std::runtime_error("ambiguous scan key");}selected=path;}
  }
  closedir(dir);require(!selected.empty(),"scan key unavailable");return selected;
}
Json savedVideo(const std::string &path,const char *card=nullptr) {
  int fd=::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return Json();
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;v4l2_capability cap{};
  bool valid=(!card || (io(fd,VIDIOC_QUERYCAP,&cap)==0 &&
    std::string(reinterpret_cast<char *>(cap.card))==card &&
    std::string(reinterpret_cast<char *>(cap.driver)).find("rkisp")==0)) && io(fd,VIDIOC_G_FMT,&f)==0;
  ::close(fd);if(!valid || f.fmt.pix_mp.width<2 || f.fmt.pix_mp.height<2)return Json();
  const auto &v=f.fmt.pix_mp;
  return {{"node",path},{"width",v.width},{"height",v.height},{"pixel_format",v.pixelformat},
    {"planes",v.num_planes},{"field",v.field},{"colorspace",v.colorspace},
    {"quantization",v.quantization},{"ycbcr_enc",v.ycbcr_enc},{"xfer_func",v.xfer_func}};
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
CaptureProfile detectedScanProfile() {
  return scanProfile(read("/proc/device-tree/model"),
                     read("/sys/class/video4linux/v4l-subdev2/name"));
}
Json scanLeaseSnapshot() {
  const auto profile = configured(detectedScanProfile());
  require(profile.supported(), "unsupported scan board or pen sensor");
  Json j{{"guardians", Json::array()}, {"captures", Json::array()},
         {"profile", profile.id}};
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
  if(profile.ov7251Startup) {
    j["video_formats"]=Json::array();
    for(const auto &v: {std::make_pair("/dev/video12","rkisp_selfpath"),std::make_pair("/dev/video11","rkisp_mainpath")}) {
      auto saved=savedVideo(v.first,v.second);if(!saved.is_null())j["video_formats"].push_back(saved);
    }
    require(!j["video_formats"].empty(),"MELON ISP capture unavailable");
    const auto gpio="/sys/class/gpio/gpio"+std::to_string(profile.gpio);
    j["gpio_export_marker"]="/tmp/penchat-scan-gpio-" PENCHAT_APP_ID "-"+std::to_string(getpid())+"-"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    j["gpio_number"]=profile.gpio;j["gpio_existed"]=access(gpio.c_str(),F_OK)==0;
    if(j["gpio_existed"].get<bool>()) {
      j["gpio_value"]=read(gpio+"/value");j["gpio_direction"]=read(gpio+"/direction");
      require(j["gpio_direction"]=="out\n","MELON scan GPIO is not an output");
      require(j["gpio_value"]=="0\n" || j["gpio_value"]=="1\n","MELON scan GPIO state unavailable");
    }
  } else {
    auto saved=savedVideo(profile.videoNode);require(!saved.is_null(),"system capture preparing");
    j["video_format"]=saved;
  }
  if(profile.light==ScanLight::LedClass)j["led_brightness"]=read(profile.ledBrightness);
  return j;
}
void setScanLight(bool on) {
  // GPIO ownership differs by board. Never export Y07 driver-owned pins.
  // Configure while arming, then toggle directly without fork/shell latency.
  const auto profile=lightProfile.supported()?lightProfile:configured(detectedScanProfile());
  require(profile.supported(), "unsupported scan light board");
  auto write = [](const char *path, const char *value) {
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
      return false;
    size_t size = strlen(value);
    bool okay = ::write(fd, value, size) == ssize_t(size);
    ::close(fd);
    return okay;
  };
  if(profile.light==ScanLight::LedClass) {
    require(!profile.ledOnValue.empty() &&
                write(profile.ledBrightness.c_str(),on?profile.ledOnValue.c_str():"0"),
            "cannot switch Y07 scan LED");return;
  }
  const auto number=std::to_string(profile.light==ScanLight::ConfiguredGpio?profile.gpio:43);
  const auto base="/sys/class/gpio/gpio"+number;
  const auto valuePath=base+"/value";const char *value=valuePath.c_str();
  if (access(value, F_OK) != 0) {
    require(write("/sys/class/gpio/export", number.c_str()), "cannot export scan light");
    if(profile.ov7251Startup) {
      // Only an export that actually succeeded belongs to this lease. In
      // particular EBUSY must never cause us to unexport a driver's GPIO.
      int marker=::open(gpioExportMarker.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
      const bool recorded=marker>=0 && ::write(marker,number.data(),number.size())==ssize_t(number.size());
      if(marker>=0)::close(marker);
      if(!recorded) {write("/sys/class/gpio/unexport",number.c_str());throw std::runtime_error("cannot record scan light ownership");}
    }
    require(write((base+"/direction").c_str(), "out"),
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
    if(j.value("profile",std::string())!="melon-ov7251")setScanLight(false);
  } catch (...) {
  }
  try {
    if (detectedScanProfile().sensor == "sc2356") {
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
    const auto profile=detectedScanProfile();
    if(profile.light==ScanLight::LedClass&&j.value("profile",std::string())==profile.id&&j.contains("led_brightness")&&j["led_brightness"].is_string()) {
      const auto value=j["led_brightness"].get<std::string>();int fd=::open(profile.ledBrightness.c_str(),O_WRONLY|O_CLOEXEC);
      if(fd>=0){::write(fd,value.data(),value.size());::close(fd);}
    }
  }catch(...){}
  try {
    auto formats=j.value("video_formats",Json::array());
    if(j.contains("video_format"))formats.push_back(j["video_format"]);
    for (const auto &saved:formats) {
      v4l2_format format{};
      format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      auto &f = format.fmt.pix_mp;
      f.width = saved.at("width"); f.height = saved.at("height");
      f.pixelformat = saved.at("pixel_format"); f.num_planes = saved.at("planes");
      f.field = saved.at("field"); f.colorspace = saved.at("colorspace");
      f.quantization = saved.at("quantization"); f.ycbcr_enc = saved.at("ycbcr_enc");
      f.xfer_func = saved.at("xfer_func");
      int video = ::open(saved.value("node",detectedScanProfile().videoNode).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (video >= 0) { io(video, VIDIOC_S_FMT, &format); ::close(video); }
    }
  } catch (...) {
  }
  try {
    if(j.value("profile",std::string())=="melon-ov7251" && j.contains("gpio_number")) {
      const auto number=std::to_string(j.at("gpio_number").get<unsigned>());
      const auto marker=j.value("gpio_export_marker",std::string());
      const bool owned=!marker.empty() && read(marker)==number;
      if(j.value("gpio_existed",false) || owned) {
        const auto path=j.value("gpio_existed",false)?"/sys/class/gpio/gpio"+number+"/value":"/sys/class/gpio/unexport";
        const auto value=j.value("gpio_existed",false)?j.value("gpio_value",std::string("0")):number;
        int fd=::open(path.c_str(),O_WRONLY|O_CLOEXEC);
        if(fd>=0){::write(fd,value.data(),value.size());::close(fd);}
      }
      if(owned)::unlink(marker.c_str());
    }
  }catch(...){}
  try {
    for (auto &p : j.value("guardians", Json::array()))
      if (!p.value("paused", false) && same(p) &&
          match(p["pid"], "guardian_run", "/usr/bin/runCapFrame"))
        kill(p["pid"].get<int>(), SIGCONT);
  } catch (...) {
  }
}
void Hardware::open(const Json &lease) {
  reserve(lease);
  start();
}
void Hardware::reserve(const Json &lease) {
  close();
  lease_ = lease;
  try {
    profile_ = configured(detectedScanProfile());
    require(profile_.supported() && lease_.value("profile", profile_.id) == profile_.id,
            "scan lease profile mismatch");
    if(profile_.ov7251Startup)
      require(profile_.gpio==lease_.at("gpio_number").get<unsigned>(),"scan light configuration changed");
    lightProfile=profile_;
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
    gpioExportMarker=lease_.value("gpio_export_marker",std::string());
    setScanLight(false);
    if(profile_.ov7251Startup)profile_.keyNode=melonKey(profile_.keyCode);
    key_ = ::open(profile_.keyNode.c_str(),
                  O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    require(key_ >= 0, "scan key unavailable");
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
  const unsigned code = profile_.keyCode;
  waitRelease_ = (keys[code / (8 * sizeof(long))] &
                  (1UL << (code % (8 * sizeof(long))))) != 0;
}
void Hardware::start() {
  try {
    require(key_ >= 0 && video_ < 0 && !lease_.empty(), "scan hardware not reserved");
    openCamera();
    // Presses made while preparing cannot become a delayed, empty scan.
    resetKey();
  } catch (...) {
    close();
    throw;
  }
}
void Hardware::openCamera() {
  // Revalidate identity before changing the capture format or sensor controls.
  auto sensorName = read("/sys/class/video4linux/v4l-subdev2/name");
  require(sensorName.find(profile_.sensor) != std::string::npos,
          "unsupported pen sensor");
  v4l2_format f{};
  auto candidates=profile_.ov7251Startup?lease_.at("video_formats"):Json::array({{{"node",profile_.videoNode}}});
  for(const auto &candidate:candidates) {
    const auto path=candidate.at("node").get<std::string>();
    video_=::open(path.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(video_<0)continue;
    f={};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    f.fmt.pix_mp.width=profile_.width;f.fmt.pix_mp.height=profile_.height;
    f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;f.fmt.pix_mp.num_planes=1;
    if(io(video_,VIDIOC_S_FMT,&f)==0 && f.fmt.pix_mp.width==unsigned(profile_.width) &&
       f.fmt.pix_mp.height==unsigned(profile_.height) && f.fmt.pix_mp.num_planes==1 &&
       f.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12) {profile_.videoNode=path;break;}
    ::close(video_);video_=-1;
  }
  require(video_>=0,"unsupported pen NV12 profile");
  stride_ = f.fmt.pix_mp.plane_fmt[0].bytesperline;
  require(stride_ >= profile_.width, "invalid pen stride");
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
  sensor_ = ::open(profile_.sensorNode.c_str(), O_RDWR | O_CLOEXEC);
  require(sensor_ >= 0, "pen sensor unavailable");
  int one = 1;
  // Both deployed kernels expose the quick-only flag. Y02's stock capture also
  // leaves it set, so STREAMON alone is not proof that pixels are arriving.
  if(!profile_.ov7251Startup)require(io(sensor_, 0x400456ea, &one) == 0, "pen quick-only failed");
  int type = f.type;
  require(io(video_, VIDIOC_STREAMON, &type) == 0, "pen STREAMON failed");
  stream_ = true;
  if(profile_.ov7251Startup) {
    startOv7251([this](unsigned ms) {
      bool any=false;unsigned consecutive=0;uint32_t previous=0;
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
      while(consecutive<16 && std::chrono::steady_clock::now()<deadline) {
        if(!readFrame([&](const uint8_t *,int,uint64_t seq,int64_t) {
          const auto current=uint32_t(seq);consecutive=any&&current==previous+1?consecutive+1:1;
          any=true;previous=current;
        }))std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return consecutive>=16?StartupFrames::Stable:any?StartupFrames::Unstable:StartupFrames::None;
    },[&] {require(io(sensor_,0x400456ca,&one)==0,"OV7251 quick stream failed");});
  } else if (profile_.sensor == "sc2356") {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(io(sensor_, 0x400456ca, &one) == 0, "pen quick stream failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(io(sensor_, 0x400456ca, &one) == 0, "pen quick stream retry failed");
  } else {
    // The deployed Y02 driver leaves SC035GS gated after pipeline STREAMON.
    // Start it once after the receiver is ready; never replay the SC2356 retry.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(io(sensor_, 0x400456ca, &one) == 0, "pen quick stream failed");
    // Verify the actual source before advertising ready. Drain startup buffers
    // here, outside any pen capture window, until the sequence is stable.
    unsigned consecutive = 0;
    uint32_t previous = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (consecutive < 16) {
      require(std::chrono::steady_clock::now() < deadline,
              "pen sensor did not deliver sustained frames");
      if (!readFrame([&](const uint8_t *, int, uint64_t sequence, int64_t) {
            const auto current = uint32_t(sequence);
            consecutive = consecutive && current == previous + 1
                              ? consecutive + 1 : 1;
            previous = current;
          }))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}
bool Hardware::keyEvent(bool &down) {
  input_event e{};
  while (::read(key_, &e, sizeof(e)) == sizeof(e)) {
    if (e.type == EV_SYN && e.code == SYN_DROPPED)
      throw std::runtime_error("scan key events lost");
    if (e.type != EV_KEY || e.code != profile_.keyCode || (e.value != 0 && e.value != 1))
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
  // Keep SC035GS/OV7251 running after startup. Toggling quick-stream at
  // pen release truncates CSI packets; the worker bounds the scan by timestamps.
  if (!profile_.gatesExposure) return;
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
                used - off >= size_t(stride_) * profile_.height * 3 / 2,
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
void Hardware::cameraToOcr() {
  require(key_ >= 0, "OCR key lease unavailable");
  resetKey();
  const int key = key_; const bool waitRelease = waitRelease_;
  key_ = -1;
  close(); // Restore camera/guardian while retaining the same evdev grab.
  key_ = key; waitRelease_ = waitRelease;
}
void Hardware::close() noexcept {
  if (sensor_ >= 0 && profile_.sensor == "sc2356") {
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
  lightProfile={};gpioExportMarker.clear();
  pressed_ = waitRelease_ = false;
}
} // namespace penchat_scan

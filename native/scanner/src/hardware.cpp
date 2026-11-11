#include "hardware.hpp"
#ifdef PENCHAT_CVI_CAPTURE
#include "cvi_capture.hpp"
#endif
#include "capture_timing.hpp"
#include <time.h>
#include "board_config.hpp"
#include "start_policy.hpp"
#include "file_read.hpp"
#include "hardware_ops.hpp"
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
using namespace hardware_detail;
namespace {
CaptureProfile configured(CaptureProfile p) {
  if (!p.boardIo.section.empty())
    return boardConfig(p,Json::parse(read("/usr/configs/hal_key_config.json")),
                         Json::parse(read("/usr/configs/hal_led_config.json")));
  return p;
}
std::string discoveredKey(unsigned code) {
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
                     read("/sys/class/video4linux/v4l-subdev2/name")+read("/usr/configs/sensor_cfg.ini"));
}
std::unique_ptr<const ResolvedDeviceProfile> resolveScanProfile(const Json &lease) {
  auto profile=configured(detectedScanProfile());
  validateProfile(profile);
  require(lease.value("profile",std::string())==profile.id,"scan lease profile mismatch");
  if (profile.light==ScanLight::ConfiguredGpio)
    require(profile.gpio==lease.at("gpio_number").get<unsigned>(),"scan light configuration changed");
  if (profile.boardIo.keyDiscovery==KeyDiscovery::NameAndCode)
    profile.keyNode=discoveredKey(profile.keyCode);
  std::vector<std::string> nodes;
  if (profile.platform==PlatformKind::Rockchip) {
    if (profile.capture.nodes==NodePolicy::RkIspPaths) {
      for (const auto &entry:lease.at("video_formats")) {
        const auto node=entry.at("node").get<std::string>();
        require(std::any_of(profile.capture.candidates.begin(),profile.capture.candidates.end(),
          [&](const std::pair<std::string,std::string> &candidate){return node==candidate.first;}),
          "unexpected scan ISP node");
        nodes.push_back(node);
      }
    } else nodes.push_back(profile.videoNode);
    require(!nodes.empty(),"scan capture nodes missing");
  }
  return std::unique_ptr<const ResolvedDeviceProfile>(new ResolvedDeviceProfile(std::move(profile),std::move(nodes)));
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
  if(profile.boardIo.gpioOwnership==GpioOwnership::RestoreExisting) {
    j["gpio_existed"]=true;
    j["gpio_number"]=profile.gpio;
    const auto base="/sys/class/gpio/gpio"+std::to_string(profile.gpio);
    j["gpio_value"]=read(base+"/value");j["gpio_direction"]=read(base+"/direction");
    require(j["gpio_direction"]=="out\n","S6 scan light not configured");
    require(j["gpio_value"]=="0\n" || j["gpio_value"]=="1\n","S6 scan light state unavailable");
  } else if(profile.capture.nodes==NodePolicy::RkIspPaths) {
    j["video_formats"]=Json::array();
    for(const auto &v:profile.capture.candidates) {
      auto saved=savedVideo(v.first,v.second.c_str());if(!saved.is_null())j["video_formats"].push_back(saved);
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
void setScanLight(const CaptureProfile &profile, const std::string &gpioExportMarker, bool on) {
  // GPIO ownership differs by board. Never export Y07 driver-owned pins.
  // Configure while arming, then toggle directly without fork/shell latency.
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
                write(profile.ledBrightness.c_str(),on?profile.ledOnValue.c_str():profile.ledOffValue.c_str()),
            "cannot switch Y07 scan LED");return;
  }
  const auto number=std::to_string(profile.gpio);
  const auto base="/sys/class/gpio/gpio"+number;
  const auto valuePath=base+"/value";const char *value=valuePath.c_str();
  if (access(value, F_OK) != 0) {
    require(write("/sys/class/gpio/export", number.c_str()), "cannot export scan light");
    if(profile.boardIo.gpioOwnership==GpioOwnership::TrackExport) {
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
  require(write(value,on?profile.ledOnValue.c_str():profile.ledOffValue.c_str()), "cannot switch scan light");
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
  CaptureProfile profile;
  try {profile=scanProfileById(j.value("profile",std::string()));} catch (...) {return;}
  if (!profile.supported()) return;
  if (profile.light==ScanLight::ConfiguredGpio) {
    try {profile.gpio=j.at("gpio_number").get<unsigned>();} catch (...) {return;}
  }
  try {
    if(profile.boardIo.gpioOwnership!=GpioOwnership::TrackExport)
      setScanLight(profile,j.value("gpio_export_marker",std::string()),false);
  } catch (...) {
  }
  try {
    if (profile.capture.startup == StartupPolicy::DoubleQuick) {
      int fd = ::open(profile.sensorNode.c_str(), O_RDWR | O_CLOEXEC);
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
      int video = ::open(saved.value("node",profile.videoNode).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (video >= 0) { io(video, VIDIOC_S_FMT, &format); ::close(video); }
    }
  } catch (...) {
  }
  try {
    if(profile.boardIo.gpioOwnership!=GpioOwnership::Retain && j.contains("gpio_number")) {
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
bool scanLeaseRestored(const Json &snapshot) noexcept {
  try {
    for(const auto &p:snapshot.value("guardians",Json::array())) {
      if(!same(p)||!match(p.at("pid"),"guardian_run","/usr/bin/runCapFrame"))continue;
      const auto state=read("/proc/"+std::to_string(p.at("pid").get<int>())+"/stat");
      const auto end=state.rfind(')');
      if(end==std::string::npos||state.size()<=end+2||state[end+2]=='T')return false;
    }
    return true;
  }catch(...){return false;}
}
} // namespace penchat_scan

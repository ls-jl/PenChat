#include "feedback.hpp"
#include "feedback_private.hpp"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <set>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace penchat {
namespace {
constexpr size_t LogLimit = 256 * 1024;
std::mutex logMutex, logIo;
Json current = Json::array(), previous = Json::array();
std::string logRoot;
bool logDirty = false;
size_t logBytes = 0;
const std::set<std::string> events = {
  "lifecycle.start", "lifecycle.background", "lifecycle.foreground", "lifecycle.stop", "lifecycle.startup_error",
  "ui.error", "camera.started", "camera.error", "camera.photo", "camera.orientation",
  "storage.save_error", "storage.save_complete", "scanner.started", "scanner.finished",
  "scanner.released", "scanner.error", "scanner.stopped", "network.request_end", "feedback.request_end", "feedback.upload_result"};
const std::set<std::string> numbers = {
  "status", "curl_code", "elapsed_ms", "dns_ms", "tcp_ms", "tls_ms", "first_byte_ms", "queue_ms",
  "new_connections", "connection_reused", "attempts", "count", "width", "height", "angle",
  "received", "filtered", "selected", "committed", "dropped_raw", "unlocated", "queue_peak", "queue_capacity",
  "raw_queued", "selected_queued", "raw_peak", "selected_peak", "frame_bytes", "canvas_bytes",
  "peak_frame_bytes", "peak_canvas_bytes", "rss_bytes", "rss_peak_bytes", "swap_bytes",
  "system_available_bytes", "system_reserve_bytes", "memory_stop_code",
  "stop_rss_bytes", "stop_swap_bytes", "stop_frame_bytes", "stop_canvas_bytes",
  "batches", "unsaved", "failed", "active", "code"};
Json safeFields(const Json &input) {
  Json out = Json::object();
  if (!input.is_object()) return out;
  for (const auto &key : numbers) {
    auto v = input.find(key);
    if (v != input.end() && (v->is_boolean() || v->is_number_integer() || v->is_number_unsigned() ||
        (v->is_number_float() && std::isfinite(v->get<double>())))) out[key] = *v;
  }
  if(out.contains("curl_code") && out["curl_code"].is_number_integer()) {
    const auto code=out["curl_code"].get<int>();
    const char *kind=code==0?"none":code==6?"dns":code==7?"connect":code==28?"timeout":
      code==35||code==60?"tls":code==42?"cancelled":"transport";
    if(code==0 && out.value("status",0)>=400)kind="http";
    out["error_category"]=kind;
  }
  return out;
}
Json safeLog(const Json &input) {
  Json out = Json::array(); size_t size = 0;
  if (!input.is_array()) return out;
  for (const auto &entry : input) {
    if (!entry.is_object() || !entry.value("category", Json()).is_string() ||
        !entry.value("event", Json()).is_string() || !entry.value("time_ms", Json()).is_number_integer()) continue;
    const auto category = entry["category"].get<std::string>(), event = entry["event"].get<std::string>();
    if (!events.count(category + "." + event)) continue;
    Json value = {{"time_ms", entry["time_ms"]}, {"category", category}, {"event", event},
                  {"fields", safeFields(entry.value("fields", Json()))}};
    const auto bytes = value.dump().size();
    while (!out.empty() && (out.size() >= 500 || size + bytes > LogLimit - 1024)) {
      size -= out.front().dump().size(); out.erase(out.begin());
    }
    out.push_back(value); size += bytes;
  }
  return out;
}
std::string text(const std::string &path, size_t max = 4096) {
  try { return fileText(path, max); } catch (...) { return {}; }
}
std::string clean(const char *data, size_t size) {
  std::string out;
  for (size_t i = 0; i < std::min(size, size_t(128)) && data[i]; ++i) {
    const auto c = static_cast<unsigned char>(data[i]);
    if (c >= 32 && c < 127) out += char(c);
  }
  return out;
}
std::vector<std::string> names(const std::string &path, const std::string &prefix, size_t limit) {
  std::vector<std::string> out;
  if (auto dir = opendir(path.c_str())) {
    while (auto e = readdir(dir)) {
      std::string name = e->d_name;
      if (name.find(prefix) != 0 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos) continue;
      if (out.size() < limit) out.push_back(name);
    }
    closedir(dir);
  }
  std::sort(out.begin(), out.end()); return out;
}
std::string fourcc(uint32_t code) {
  char value[5] = {char(code), char(code >> 8), char(code >> 16), char(code >> 24), 0};
  return clean(value, 4);
}
}

std::string feedbackUuid() {
  unsigned char bytes[16];
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) throw std::runtime_error("无法生成反馈编号");
  size_t used = 0;
  while (used < sizeof(bytes)) {
    auto n = read(fd, bytes + used, sizeof(bytes) - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { close(fd); throw std::runtime_error("无法生成反馈编号"); }
    used += size_t(n);
  }
  close(fd); bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
  std::ostringstream out; out << std::hex << std::setfill('0');
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out << '-';
    out << std::setw(2) << unsigned(bytes[i]);
  }
  return out.str();
}
void feedbackWrite(const std::string &path, const Json &value) {
  const auto content = value.dump(-1, ' ', false, Json::error_handler_t::replace);
  const auto temporary = path + "." + feedbackUuid() + ".part";
  int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) throw std::runtime_error("无法保存反馈记录");
  bool okay = true; size_t used = 0;
  while (used < content.size()) {
    auto n = write(fd, content.data() + used, content.size() - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { okay = false; break; }
    used += size_t(n);
  }
  if (okay) okay = fsync(fd) == 0;
  close(fd);
  if (!okay || rename(temporary.c_str(), path.c_str())) {
    unlink(temporary.c_str()); throw std::runtime_error("无法保存反馈记录");
  }
  const auto parent = path.substr(0, path.rfind('/'));
  int dir = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir >= 0) { fsync(dir); close(dir); }
}
Json feedbackRead(const std::string &path, size_t limit) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return Json();
  struct stat st{};
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || size_t(st.st_size) > limit) { close(fd); return Json(); }
  std::string content; char buffer[4096];
  for (;;) {
    auto n = read(fd, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 || content.size() + size_t(std::max<ssize_t>(0,n)) > limit) { close(fd); return Json(); }
    if (!n) break;
    content.append(buffer, size_t(n));
  }
  close(fd); return Json::parse(content, nullptr, false);
}
void beginDiagnostics(const std::string &directory) noexcept {
  try {
    {std::lock_guard<std::mutex> guard(logMutex);logRoot.clear();current=previous=Json::array();logBytes=0;logDirty=false;}
    const auto root = join(directory, ".feedback");
    struct stat st{};
    if (lstat(root.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) return;
    ensureDirectory(root);
    auto old = feedbackRead(join(root,"diagnostic-current.json"), LogLimit + 16384);
    auto before = safeLog(old);
    feedbackWrite(join(root,"diagnostic-previous.json"), before);
    std::lock_guard<std::mutex> guard(logMutex);
    logRoot = root; current = Json::array(); previous = before; logBytes = 0; logDirty = true;
  } catch (...) { /* A diagnostics directory must never prevent startup. */ }
  diagnosticRecord("lifecycle", "start"); flushDiagnostics();
}
void diagnosticRecord(const std::string &category, const std::string &event, const Json &fields) noexcept {
  try {
    if (!events.count(category + "." + event)) return;
    Json entry = {{"time_ms",nowMs()},{"category",category},{"event",event},{"fields",safeFields(fields)}};
    const auto bytes = entry.dump().size();
    std::lock_guard<std::mutex> guard(logMutex);
    while (!current.empty() && (current.size() >= 500 || logBytes + bytes > LogLimit - 1024)) {
      logBytes -= current.front().dump().size(); current.erase(current.begin());
    }
    current.push_back(std::move(entry)); logBytes += bytes; logDirty = true;
  } catch (...) {}
}
void flushDiagnostics() noexcept {
  try {
    std::lock_guard<std::mutex> io(logIo);
    Json value; std::string root;
    { std::lock_guard<std::mutex> guard(logMutex); if (!logDirty || logRoot.empty()) return;
      root = logRoot; value = current; logDirty = false; }
    try { feedbackWrite(join(root,"diagnostic-current.json"), value); }
    catch (...) { std::lock_guard<std::mutex> guard(logMutex); logDirty = true; }
  } catch (...) {}
}
Json diagnosticRecords() {
  std::lock_guard<std::mutex> guard(logMutex);
  return {{"previous_run",previous},{"current_run",current}};
}
FeedbackConfig FeedbackConfig::build() {
  FeedbackConfig config;
  config.endpoint = FeedbackBuild_endpoint; config.token = FeedbackBuild_token;
  config.nodes = Json::parse(FeedbackBuild_nodes).get<std::vector<std::string>>();
#ifndef __aarch64__
  if (const auto path = getenv("PENCHAT_TEST_FEEDBACK_CONFIG")) {
    const auto data = feedbackRead(path, 16384);
    if (data.is_object()) {
      config.endpoint = data.value("endpoint",std::string()); config.token = data.value("token",std::string());
      config.nodes = data.value("nodes",std::vector<std::string>());
      config.retryUnitMs = std::max(1, std::min(1000, data.value("retry_unit_ms",1000)));
    }
  }
#endif
  return config;
}
Json feedbackScreen(const Json &configuration, const Screen &s) {
  Json original = Json::object();
  const auto c = configuration.value("screen",Json::object());
  for (const auto key : {"width","height","direction","xoffset","yoffset","tp_direction","tp_xoffset","tp_yoffset","dbuffer"}) {
    auto it = c.find(key);
    if (it != c.end() && (it->is_number_integer() || it->is_boolean())) original[key] = *it;
  }
  auto effective = s.descriptor();
  effective["touch_rotation"] = s.touchRotation; effective["touch_xoffset"] = s.touchXoffset;
  effective["touch_yoffset"] = s.touchYoffset;
  effective["lvgl_buffer_count"] = 1; // Display supplies one LVGL full-frame buffer.
  return {{"source","falcon_configuration"},{"configured",original},{"effective",effective}};
}
Json feedbackHardware(bool native) {
  Json out = {{"input_devices",Json::array()},{"video_devices",Json::array()}};
  utsname u{};
  if (!uname(&u)) out["system"] = {{"kernel",clean(u.release,sizeof(u.release))},{"architecture",clean(u.machine,sizeof(u.machine))}};
  if (!native) { out["status"] = "host_test"; return out; }
  auto model = text("/proc/device-tree/model");
  out["model"] = clean(model.data(),model.size());
  if(model.empty())out["model_status"]="unknown: device-tree model unavailable";
  auto compatibles = text("/proc/device-tree/compatible"); out["compatible"] = Json::array();
  std::istringstream list(compatibles); std::string part;
  while (out["compatible"].size() < 16 && std::getline(list,part,'\0')) out["compatible"].push_back(clean(part.data(),part.size()));
  if(compatibles.empty())out["compatible_status"]="unknown: device-tree compatible unavailable";
  for (const auto &name : names("/dev/input","event",128)) {
    const auto path = "/dev/input/" + name;
    int fd = open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    Json device = {{"path",path}};
    if (fd < 0) { device["errno"] = errno; device["status"] = "unavailable"; out["input_devices"].push_back(device); continue; }
    char title[256]{}; input_id id{};
    if (ioctl(fd, EVIOCGNAME(sizeof(title)), title) >= 0) device["name"] = clean(title,sizeof(title));
    if (ioctl(fd, EVIOCGID, &id) == 0) device["id"] = {{"bus",id.bustype},{"vendor",id.vendor},{"product",id.product},{"version",id.version}};
    unsigned char bits[(KEY_MAX+8)/8]{};
    if (ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits) >= 0) {
      device["key_codes"] = Json::array();
      for (int code = 0; code <= KEY_MAX; ++code) if (bits[code/8] & (1u << (code%8))) device["key_codes"].push_back(code);
    }
    device["axes"] = Json::object();
    for (int code : {ABS_X,ABS_Y,ABS_MT_POSITION_X,ABS_MT_POSITION_Y,ABS_MT_SLOT,ABS_MT_TRACKING_ID}) {
      input_absinfo axis{};
      if (ioctl(fd,EVIOCGABS(code),&axis) == 0)
        device["axes"][std::to_string(code)] = {{"minimum",axis.minimum},{"maximum",axis.maximum},{"resolution",axis.resolution}};
    }
    close(fd); out["input_devices"].push_back(device);
  }
  for (const auto &name : names("/sys/class/video4linux","",128)) {
    if (name.find("video") != 0 && name.find("v4l-subdev") != 0) continue;
    const auto path = "/dev/" + name;
    const auto title = text("/sys/class/video4linux/" + name + "/name");
    Json device = {{"path",path},{"name",clean(title.data(),title.size())}};
    int fd = open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if (fd < 0) { device["status"] = "unavailable"; device["errno"] = errno; out["video_devices"].push_back(device); continue; }
    if (name.find("v4l-subdev") == 0) {
      v4l2_subdev_format f{}; f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
      if (ioctl(fd,VIDIOC_SUBDEV_G_FMT,&f) == 0) device["current"] = {{"width",f.format.width},{"height",f.format.height},{"media_bus_code",f.format.code}};
      else { device["status"] = "unknown_format"; device["errno"] = errno; }
    } else {
      v4l2_capability cap{};
      if (ioctl(fd,VIDIOC_QUERYCAP,&cap) == 0) {
        device["driver"] = clean(reinterpret_cast<char *>(cap.driver),sizeof(cap.driver));
        device["card"] = clean(reinterpret_cast<char *>(cap.card),sizeof(cap.card));
        const auto caps = cap.device_caps ? cap.device_caps : cap.capabilities; device["capabilities"] = caps;
        if (caps & (V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
          const unsigned type = caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
          v4l2_format f{}; f.type = type;
          if (ioctl(fd,VIDIOC_G_FMT,&f) == 0) {
            const auto w = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? f.fmt.pix_mp.width : f.fmt.pix.width;
            const auto h = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? f.fmt.pix_mp.height : f.fmt.pix.height;
            const auto code = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? f.fmt.pix_mp.pixelformat : f.fmt.pix.pixelformat;
            device["current"] = {{"width",w},{"height",h},{"fourcc",fourcc(code)}};
          } else { device["status"] = "unknown_format"; device["errno"] = errno; }
          device["formats"] = Json::array();
          for (unsigned i = 0; i < 8; ++i) {
            v4l2_fmtdesc fmt{}; fmt.type = type; fmt.index = i;
            if (ioctl(fd,VIDIOC_ENUM_FMT,&fmt) < 0) break;
            Json row = {{"fourcc",fourcc(fmt.pixelformat)},{"sizes",Json::array()}};
            for (unsigned n = 0; n < 8; ++n) {
              v4l2_frmsizeenum size{}; size.index = n; size.pixel_format = fmt.pixelformat;
              if (ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&size) < 0) break;
              if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) row["sizes"].push_back({{"width",size.discrete.width},{"height",size.discrete.height}});
              else { row["sizes"].push_back({{"min_width",size.stepwise.min_width},{"max_width",size.stepwise.max_width},{"min_height",size.stepwise.min_height},{"max_height",size.stepwise.max_height}}); break; }
            }
            device["formats"].push_back(row);
          }
        }
      } else { device["status"] = "unavailable"; device["errno"] = errno; }
    }
    close(fd); out["video_devices"].push_back(device);
  }
  return out;
}
} // namespace penchat

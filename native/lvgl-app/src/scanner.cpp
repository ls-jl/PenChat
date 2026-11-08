#include "scanner.hpp"
#include "../../scanner/src/hardware.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace penchat {
using namespace penchat_scan;
struct ScanService::Impl {
  Events &events;
  std::string workspace, data, binary, resources;
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::thread worker;
  bool desiredOcr = false;
  bool stopped = false, desired = false, disarmed = true, available = false;
  std::string session;
  uint64_t generation = 0, request = 0;
  Json status = Json::object(), lease;
  const CaptureProfile profile=detectedScanProfile();
  const bool sharedMedia=profile.cvi;
  uint64_t mediaToken=0,mediaAck=0;bool displayHeld=false;
  void yieldDisplay() {
    if(!sharedMedia||displayHeld)return;
    std::unique_lock<std::mutex> lock(mutex);const auto token=++mediaToken;
    events.push("scan_media_release",{{"token",token}});
    if(!cv.wait_for(lock,std::chrono::seconds(3),[&]{return mediaAck>=token||stopped;}))
      throw std::runtime_error("native display handoff timed out");
    displayHeld=true;
  }
  void resumeDisplay(bool systemOwner=false) {
    if(!sharedMedia||!displayHeld||stopped)return;
    if(systemOwner) {
      // The stock owner must finish rebuilding its two VPSS outputs before a
      // new VO file description is opened. Never race its broad SYS init.
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
      int stable=0;
      while(std::chrono::steady_clock::now()<end) {
        const auto text=fileText("/proc/cvitek/vpss",65536);
        if(text.find("NV12")!=std::string::npos&&text.find("YUV_400")!=std::string::npos)++stable;else stable=0;
        if(stable>=3)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if(stable<3)throw std::runtime_error("system capture recovery timed out");
    }
    displayHeld=false;events.push("scan_media_resume");
  }
  std::deque<Json> commands;
  int socket = -1, memory = -1, processLock = -1;
  pid_t child = -1;
  PreviewSlot *slot = nullptr;
  Impl(Events &e, std::string w, std::string d)
      : events(e), workspace(std::move(w)), data(std::move(d)) {
    binary = join(workspace, "assets/bin/penchat-scanner");
    resources = join(workspace, "assets/scanner");
#ifndef PENCHAT_NATIVE_DEVICE
    if (const auto p = getenv("PENCHAT_SCANNER_BINARY"))
      binary = p;
    if (const auto p = getenv("PENCHAT_SCAN_RESOURCES"))
      resources = p;
#endif
    available = access(binary.c_str(), R_OK) == 0 &&
                access(join(resources, detectedScanProfile().cvi ? "pintu_rgb32_int8.cvimodel" : "demo_gain.json").c_str(), R_OK) == 0;
#ifdef PENCHAT_NATIVE_DEVICE
    // Only explicit board/sensor capture profiles may own the pen hardware.
    available = available && detectedScanProfile().supported();
#else
    available = available && getenv("PENCHAT_SCANNER_BINARY");
#endif
    worker = std::thread([this] { run(); });
  }
  ~Impl() { stop(); }
  void stop() {
    {
      std::lock_guard<std::mutex> l(mutex);
      stopped = true;
      cv.notify_all();
    }
    if(worker.joinable())worker.join();
  }
  Json command(const std::string &type, uint64_t scan = 0) {
    return {{"v", ProtocolVersion},     {"type", type},
            {"generation", generation}, {"session", session},
            {"requestId", ++request},   {"scanId", scan}};
  }
  void queue(const std::string &type, uint64_t scan = 0) {
    commands.push_back(command(type, scan));
    cv.notify_all();
  }
  void start() {
    bool mock = false;
#ifndef PENCHAT_NATIVE_DEVICE
    mock = getenv("PENCHAT_SCANNER_TEST") != nullptr;
#endif
    if (!mock)
      processLock = acquireScanProcessLock(3500);
    // AMR extraction does not consistently preserve executable mode.
    if (chmod(binary.c_str(), 0755))
      throw std::runtime_error("cannot make scanner executable");
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair))
      throw std::runtime_error("scanner socketpair failed");
    socket = pair[0];
    memory = int(syscall(SYS_memfd_create, "penchat-scan-preview", 1));
    if (memory < 0 || ftruncate(memory, sizeof(PreviewSlot))) {
      close(pair[1]);
      throw std::runtime_error("scanner preview allocation failed");
    }
    slot = static_cast<PreviewSlot *>(mmap(nullptr, sizeof(PreviewSlot),
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           memory, 0));
    if (slot == MAP_FAILED) {
      slot = nullptr;
      close(pair[1]);
      throw std::runtime_error("scanner preview mmap failed");
    }
    new (slot) PreviewSlot;
    int control = fcntl(pair[1], F_DUPFD_CLOEXEC, 10);
    int shared = fcntl(memory, F_DUPFD_CLOEXEC, 10);
    int lockCopy =
        processLock >= 0 ? fcntl(processLock, F_DUPFD_CLOEXEC, 10) : -1;
    close(pair[1]);
    if (control < 0 || shared < 0 || (processLock >= 0 && lockCopy < 0)) {
      if (control >= 0)
        close(control);
      if (shared >= 0)
        close(shared);
      if (lockCopy >= 0)
        close(lockCopy);
      throw std::runtime_error("scanner fd duplication failed");
    }
    const auto pending = join(data, ".scan-pending");
    std::vector<std::string> args{binary,         "--bridge-fd", "3",
                                  "--preview-fd", "4",           "--resources",
                                  resources,      "--data-dir",  pending};
    if (lockCopy >= 0) {
      args.push_back("--lease-fd");
      args.push_back("5");
    }
#ifndef PENCHAT_NATIVE_DEVICE
    if (getenv("PENCHAT_SCANNER_TEST"))
      args.push_back("--simulate");
#endif
    std::vector<char *> argv;
    for (auto &a : args)
      argv.push_back(&a[0]);
    argv.push_back(nullptr);
    std::vector<std::string> environment;
    for (char **p = environ; *p; ++p) {
      const std::string value = *p;
      bool replace = false;
      for (const char *name : {"LD_LIBRARY_PATH=", "LD_PRELOAD=", "LD_AUDIT=",
                               "OPENCV_OPENCL_RUNTIME=",
                               "OPENBLAS_NUM_THREADS=", "OMP_NUM_THREADS="})
        if (value.find(name) == 0)
          replace = true;
      if (!replace)
        environment.push_back(value);
    }
    std::string libraries = join(resources, "lib");
#ifndef PENCHAT_NATIVE_DEVICE
    if (const char *p = getenv("LD_LIBRARY_PATH"))
      libraries = std::string(p) + ":" + libraries;
#endif
    environment.push_back("LD_LIBRARY_PATH=" + libraries);
    environment.push_back("OPENCV_OPENCL_RUNTIME=disabled");
    environment.push_back("OPENBLAS_NUM_THREADS=1");
    environment.push_back("OMP_NUM_THREADS=1");
    std::vector<char *> env;
    for (auto &v : environment)
      env.push_back(&v[0]);
    env.push_back(nullptr);
    // Prepare every C++ allocation before fork. The child only invokes
    // async-signal-safe fd/exec functions and inherits no AMR/DRM/database fds.
    char **arguments = argv.data(), **variables = env.data();
    const char *program = binary.c_str();
    rlimit limit{65536, 65536};
    getrlimit(RLIMIT_NOFILE, &limit);
    const int maxFd = int(std::min<rlim_t>(limit.rlim_cur, 65536));
    child = fork();
    if (child == 0) {
      if (dup2(control, 3) < 0 || dup2(shared, 4) < 0)
        _exit(126);
      if (lockCopy >= 0 && dup2(lockCopy, 5) < 0)
        _exit(126);
      int out = open("/dev/null", O_RDWR);
      if (out >= 0) {
        dup2(out, 0);
        dup2(out, 1);
        dup2(out, 2);
      }
      for (int n = lockCopy >= 0 ? 6 : 5; n < maxFd; ++n)
        close(n);
      execve(program, arguments, variables);
      _exit(127);
    }
    close(control);
    close(shared);
    if (lockCopy >= 0)
      close(lockCopy);
    if (child < 0)
      throw std::runtime_error("cannot start scanner");
  }
  void cleanup() {
    if(sharedMedia)try{yieldDisplay();}catch(...){}
    if (socket >= 0) {
      close(socket);
      socket = -1;
    }
    if (child > 0) {
      bool done = false;
      for (int i = 0; i < 400; ++i) {
        auto waited = waitpid(child, nullptr, WNOHANG);
        if (waited == child || (waited < 0 && errno == ECHILD)) {
          done = true;
          break;
        }
        if (i == 200)
          kill(child, SIGTERM);
        usleep(5000);
      }
      if (!done && waitpid(child, nullptr, WNOHANG) == 0) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
      }
      const auto directory = join(data, ".scan-pending");
      const auto prefix = "scan_" + std::to_string(child) + "_";
      if (auto d = opendir(directory.c_str())) {
        while (auto e = readdir(d))
          if (std::string(e->d_name).find(prefix) == 0)
            unlink(join(directory, e->d_name).c_str());
        closedir(d);
      }
      child = -1;
    }
    // Last resort after a killed child, never before its light worker exits.
    if (!lease.empty()) {
      restoreScanLease(lease);
      if(sharedMedia&&scanLeaseRestored(lease))unlink(join(data,"scanner-lease.json").c_str());
    }
    lease = Json();
    // Keep the shared flock through waitpid and fallback restoration. A new
    // worker must not acquire the same guardian while old cleanup is pending.
    if (processLock >= 0)
      close(processLock);
    processLock = -1;
    {
      std::lock_guard<std::mutex> l(mutex);
      if (slot)
        munmap(slot, sizeof(PreviewSlot));
      slot = nullptr;
      disarmed = true;
      status = {{"state", "disarmed"}};
    }
    if (memory >= 0)
      close(memory);
    memory = -1;
  }
  void run() {
    for (;;) {
      std::deque<Json> outgoing;
      bool idle = false;
      {
        std::unique_lock<std::mutex> l(mutex);
        if (commands.empty() && socket < 0 && !stopped)
          cv.wait(l, [&] { return stopped || !commands.empty(); });
        else if (commands.empty() && disarmed && !desiredOcr && !stopped)
          cv.wait_for(l, std::chrono::milliseconds(250),
                      [&] { return stopped || !commands.empty(); });
        if (stopped)
          break;
        outgoing.swap(commands);
        idle = disarmed && !desiredOcr && outgoing.empty();
      }
      try {
        if (socket < 0 && !outgoing.empty())
          start();
        for (auto &j : outgoing) {
          const auto type=j.value("type",std::string());
          if(type=="disarm"||type=="cancel")yieldDisplay();
          if (!sendPacket(socket, j))throw std::runtime_error("scanner command failed");
        }
        if (socket < 0)
          continue;
        pollfd p{socket, POLLIN, 0};
        poll(&p, 1, idle ? 0 : 10);
        if (p.revents & (POLLHUP | POLLERR | POLLNVAL))
          throw std::runtime_error("scanner process exited");
        for (int i = 0; i < 32; ++i) {
          auto j = receivePacket(socket);
          if (j.is_null())
            break;
          if (j.is_boolean())
            throw std::runtime_error("scanner disconnected");
          j["worker_pid"] = child;
          auto type = j.value("type", std::string());
          if (type == "fatal")
            throw std::runtime_error(
                j.value("error", std::string("scanner failed")));
          if(type=="media_yield") {
            yieldDisplay();auto ack=j;ack["type"]="media_ack";
            if(!sendPacket(socket,ack))throw std::runtime_error("media release acknowledgement failed");
            continue;
          }
          if (type == "lease") {
            yieldDisplay();
            lease = j.at("lease");
            if(sharedMedia)atomicText(join(data,"scanner-lease.json"),lease.dump());
            Json ack = j;
            ack["type"] = "lease_ack";
            ack.erase("lease");
            if (!sendPacket(socket, ack))
              throw std::runtime_error("scanner lease handshake failed");
          }
          {
            std::lock_guard<std::mutex> l(mutex);
            if (j.value("generation", uint64_t(0)) != generation ||
                j.value("session", std::string()) != session)
              continue;
            status = j;
            if (type == "released") {
              disarmed = !sharedMedia;
              if(sharedMedia&&scanLeaseRestored(lease))unlink(join(data,"scanner-lease.json").c_str());
              lease = Json();
            }
            if (type == "error" || type == "fatal")
              disarmed = !sharedMedia;
          }
          if(type=="ready")resumeDisplay();
          if(type=="released"||type=="error") {
            resumeDisplay(true);
            std::lock_guard<std::mutex> l(mutex);disarmed=true;
          }
          if (type == "preview" || type == "progress")
            events.pushLatest("scan_" + type, j);
          else
            events.push("scan_" + type, j);
          // Keep the worker and model resident until application shutdown.
          // released only acknowledges camera/key/light and guardian handoff.
        }
      } catch (const std::exception &e) {
        cleanup();
        Json j;
        {
          std::lock_guard<std::mutex> l(mutex);
          j = command("error");
          j["error"] = e.what();
          status = j;
        }
        try{resumeDisplay(true);}catch(...){}
        events.push("scan_error", j);
      }
    }
    cleanup();
  }
};
void ScanService::recoverHardware(const std::string &data) {
  if(!detectedScanProfile().cvi)return;
  const auto path=join(data,"scanner-lease.json");
  if(access(path.c_str(),F_OK))return;
  const int lock=acquireScanProcessLock(2000);
  struct Close {int fd;~Close(){::close(fd);}} guard{lock};
  const auto lease=Json::parse(fileText(path,65536));
  if(lease.value("profile",std::string())!="s6-pag7920")
    throw std::runtime_error("Unexpected capture recovery profile");
  restoreScanLease(lease);
  if(!scanLeaseRestored(lease))throw std::runtime_error("System capture recovery failed");
  unlink(path.c_str());
}
ScanService::ScanService(Events &e, const std::string &w, const std::string &d)
    : p_(new Impl(e, w, d)) {}
ScanService::~ScanService() = default;
void ScanService::stop() { p_->stop(); }
void ScanService::mediaReleased(uint64_t token) {
  std::lock_guard<std::mutex> lock(p_->mutex);p_->mediaAck=std::max(p_->mediaAck,token);p_->cv.notify_all();
}
void ScanService::eligible(bool enabled, const std::string &session, bool ocrKeys) {
  std::lock_guard<std::mutex> l(p_->mutex);
  if (!p_->available || p_->stopped)
    return;
  enabled = enabled || ocrKeys;
  if (enabled && (!p_->desired || p_->session != session || p_->desiredOcr != ocrKeys)) {
    p_->desired = true;
    p_->desiredOcr = ocrKeys;
    p_->session = session;
    ++p_->generation;
    p_->disarmed = false;
    p_->status = {{"type", "preparing"},
                  {"generation", p_->generation},
                  {"session", session}};
    p_->queue(ocrKeys ? "ocr_keys" : "arm");
  } else if (!enabled && p_->desired) {
    p_->desired = false;
    p_->desiredOcr = false;
    p_->queue("disarm");
  }
}
uint64_t ScanService::prepareReview(uint64_t scan) {
  std::lock_guard<std::mutex> l(p_->mutex);
  auto j=p_->command("prepare_review",scan);auto id=j["requestId"].get<uint64_t>();
  p_->commands.push_back(std::move(j));p_->cv.notify_all();return id;
}
void ScanService::cancelReview(uint64_t scan) {
  std::lock_guard<std::mutex> l(p_->mutex);
  if(!p_->disarmed)p_->queue("cancel_review",scan);
}
void ScanService::confirm(uint64_t scan) {
  std::lock_guard<std::mutex> l(p_->mutex);
  p_->queue("confirm", scan);
}
void ScanService::discard(uint64_t scan) {
  std::lock_guard<std::mutex> l(p_->mutex);
  p_->queue("discard", scan);
}
bool ScanService::released() const {
  std::lock_guard<std::mutex> l(p_->mutex);
  return p_->disarmed;
}
bool ScanService::nativeOcrKeys() const {
#ifndef PENCHAT_NATIVE_DEVICE
  if(const char *test=getenv("PENCHAT_TEST_NATIVE_OCR_KEYS"))return std::string(test)=="1";
#endif
  return p_->profile.id=="y07-sc2356";
}
bool ScanService::available() const { return p_->available; }
uint64_t ScanService::generation() const {
  std::lock_guard<std::mutex> l(p_->mutex);
  return p_->generation;
}
Json ScanService::state() const {
  std::lock_guard<std::mutex> l(p_->mutex);
  return p_->status;
}
#ifndef PENCHAT_NATIVE_DEVICE
void ScanService::test(const Json &input) {
  if (!getenv("PENCHAT_SCANNER_TEST"))
    return;
  std::lock_guard<std::mutex> l(p_->mutex);
  auto type = input.value("command", std::string());
  if (type != "test.press" && type != "test.frame" && type != "test.memory")
    return;
  auto j = p_->command(type);
  j["pressed"] = input.value("pressed", false);
  j["sequence"] = input.value("sequence", uint64_t(0));
  if (type == "test.memory") j["state"] = input.value("state", 0);
  if (input.contains("rawSequence"))
    j["rawSequence"] = input.at("rawSequence");
  p_->commands.push_back(j);
  p_->cv.notify_all();
}
#endif
std::shared_ptr<ImageFrame> ScanService::preview(uint64_t scan) {
  std::lock_guard<std::mutex> l(p_->mutex);
  if (!p_->slot)
    return {};
  auto &s = *p_->slot;
  for (int retry = 0; retry < 3; ++retry) {
    auto before = s.sequence.load(std::memory_order_acquire);
    if (!before || before & 1)
      continue;
    auto w = s.width, h = s.height;
    if (!w || !h || w > PreviewWidth || h > PreviewHeight || s.scan != scan ||
        s.generation != p_->generation)
      return {};
    auto image = std::make_shared<ImageFrame>();
    image->width = w;
    image->height = h;
    image->pixels.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      image->pixels[i * 4] = image->pixels[i * 4 + 1] =
          image->pixels[i * 4 + 2] = s.pixels[i];
      image->pixels[i * 4 + 3] = 255;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    if (s.sequence.load(std::memory_order_relaxed) == before)
      return image;
  }
  return {};
}
} // namespace penchat

#include "jquick_config.h"
#include "JQuickContext.h"
#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <dlfcn.h>
#include "update.hpp"
namespace JQuick { class PackageInfo; }
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace JQUTIL_NS;
namespace {
void saveInstallTicket(const std::string &path,const penchat::Json &ticket) {
  penchat::atomicText(path,ticket.dump());
  int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("Cannot sync install ticket");
  const int result=fsync(fd);close(fd);if(result)throw std::runtime_error("Cannot sync install ticket");
  const auto parent=path.substr(0,path.find_last_of('/'));fd=open(parent.c_str(),O_DIRECTORY|O_RDONLY|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("Cannot sync install directory");
  const int synced=fsync(fd);close(fd);if(synced)throw std::runtime_error("Cannot sync install directory");
}
class Launcher : public JQBaseObject {
  std::mutex mutex_;
  int fd_ = -1;
  pid_t pid_ = -1;
  std::string ownId_, ownData_;
  std::set<std::string> installations_;
  void stopLocked() {
    if (fd_ >= 0) {
      shutdown(fd_, SHUT_RDWR);
      close(fd_);
      fd_ = -1;
    }
    if (pid_ > 0) {
      int status = 0;
      auto result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD))
        pid_ = -1;
    }
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      // Permit bounded HTTP cancellation, scanner lease restoration and DB
      // final-save acknowledgement before falling back to SIGKILL.
      for (int i = 0; i < 500; ++i) {
        int status;
        auto result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_ || result < 0) {
          pid_ = -1;
          break;
        }
        usleep(10000);
      }
      if (pid_ > 0) {
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
      }
    }
  }

public:
  void OnCtor(JQFunctionInfo &info) override {
    ownId_=JQuick::getAppid(info.GetContext());ownData_=JQuick::getDataDir(info.GetContext());
  }
  void install(JQAsyncInfo &info) {
    try {
      const auto ticket=penchat::Json::parse(info[0].dump());
      const auto operation=ticket.at("operation").get<std::string>();
      const auto version=ticket.at("version").get<std::string>();
      const auto path=penchat::join(penchat::join(ownData_,".updates"),"package.amr");
      penchat::updates::Version parsed;
      if(ownId_.empty()||ticket.at("appid")!=ownId_||ticket.at("path")!=path||
         operation.size()!=36||operation.find_first_not_of("0123456789abcdefABCDEF-")!=std::string::npos||
         !penchat::updates::Version::parse(version,parsed)||ticket.at("sha256").get<std::string>().size()!=64)
        throw std::runtime_error("Invalid self-update identity");
      std::lock_guard<std::mutex> lock(mutex_);
      if(installations_.count(operation)||installations_.size()>=8)throw std::runtime_error("Installation already submitted");
      std::atomic<bool> cancel{false};
      penchat::updates::validatePackage(path,ownId_,version,ticket.at("sha256"),cancel);
      // Use exported Falcon methods, never object offsets, fixed addresses,
      // a forged app identity, system-policy modifications or a child command.
      using Instance=void *(*)();
      using Complete=std::function<void(int,const JQuick::PackageInfo &)>;
      using Install=void (*)(void *,const std::string &,Complete,int);
      auto instance=reinterpret_cast<Instance>(dlsym(RTLD_DEFAULT,"_ZN6JQuick17IotPackageManager8instanceEv"));
      auto submit=reinterpret_cast<Install>(dlsym(RTLD_DEFAULT,"_ZN6JQuick17IotPackageManager14installPackageERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt8functionIFviRKNS_11PackageInfoEEEi"));
      if(!instance||!submit)throw std::runtime_error("Falcon native package API is unavailable");
      auto manager=instance();if(!manager)throw std::runtime_error("Falcon package manager is unavailable");
      auto accepted=ticket;accepted["state"]="accepted";
      const auto record=penchat::join(penchat::join(ownData_,".updates"),"install.json");saveInstallTicket(record,accepted);
      installations_.insert(operation);
      submit(manager,path,[info,accepted,record](int code,const JQuick::PackageInfo &) {
        auto result=accepted;result["state"]=code==0?"installed":"failed";
        try{saveInstallTicket(record,result);}catch(...){}
        info.post(Bson::object{{"res",code}});
      },0);
    }catch(const std::exception &error){info.postError(error.what());}
  }
  ~Launcher() override {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
  }
  void start(JQAsyncInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
    auto o = info[0];
    auto workspace = o["workspace"].string_value(),
         data = o["dataDir"].string_value();
    if (workspace.empty() || data.empty()) {
      info.postError("Missing application paths");
      return;
    }
    auto binary = workspace + "/assets/bin/penchat-lvgl";
    chmod(binary.c_str(), 0755);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) {
      info.postError("socketpair failed");
      return;
    }
    const auto descriptor = std::to_string(pair[1]);
    const auto log = data + "/lvgl-runtime.log";
    rlimit limits{};
    getrlimit(RLIMIT_NOFILE, &limits);
    const int maxFd = std::min<rlim_t>(limits.rlim_cur, 65536);
    auto parent = getpid();
    auto child = fork();
    if (child == 0) {
      close(pair[0]);
      if (getppid() != parent)
        _exit(126);
      fcntl(pair[1], F_SETFD, 0);
      int out = open(log.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
      if (out >= 0) {
        dup2(out, STDERR_FILENO);
        dup2(out, STDOUT_FILENO);
        close(out);
      }
      for (int fd = 3; fd < maxFd; ++fd)
        if (fd != pair[1])
          close(fd);
      execl(binary.c_str(), binary.c_str(), "--bridge-fd", descriptor.c_str(),
            "--data-dir", data.c_str(), "--workspace", workspace.c_str(),
            nullptr);
      _exit(127);
    }
    close(pair[1]);
    if (child < 0) {
      close(pair[0]);
      info.postError("fork failed");
      return;
    }
    pid_ = child;
    fd_ = pair[0];
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
    info.post(Bson::object{{"ok", true}, {"pid", int(child)}});
  }
  void reopen(JQFunctionInfo &info) {
    const auto id=JQuick::getAppid(info.GetContext());
    info.GetReturnValue().Set(true);
    JQuick::closeApp(id);
    JQuick::startApp(id,"",{});
  }
  void send(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto input = info.toParamsHolder();
    auto payload = input[0].dump();
    bool ok = fd_ >= 0 && payload.size() <= 65536 &&
              ::send(fd_, payload.data(), payload.size(),
                     MSG_NOSIGNAL | MSG_DONTWAIT) == ssize_t(payload.size());
    info.GetReturnValue().Set(ok);
  }
  void status(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ > 0) {
      int code = 0;
      auto result = waitpid(pid_, &code, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD)) {
        pid_ = -1;
        if (fd_ >= 0) {
          close(fd_);
          fd_ = -1;
        }
      }
    }
    info.GetReturnValue().Set(
        Bson::object{{"alive", pid_ > 0 && fd_ >= 0}, {"pid", int(pid_)}});
  }
  void ocrStroke(JQFunctionInfo &info) {
    const auto parameters = info.toParamsHolder();
    const bool down = parameters[0].bool_value();
    const int sequence = parameters[1].int_value();
    using Instance = void *(*)();
    using Stroke = void (*)(void *, unsigned, bool);
    // Falcon loads vendor business modules locally on some firmware. Resolve
    // within the already loaded module; never load a second OCR engine.
    void *module = dlopen("libyddal_business_core.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!module) module = dlopen("/oem/YoudaoDictPen/output/libs/libyddal_business_core.so", RTLD_LAZY | RTLD_NOLOAD);
    void *scope = module ? module : RTLD_DEFAULT;
    const auto instance = reinterpret_cast<Instance>(dlsym(scope,"_ZN7YDALAPI11YOCRManager8InstanceEv"));
    const auto press = reinterpret_cast<Stroke>(dlsym(scope,"_ZN7YDALAPI11YOCRManager17onHeadButtonPressEjb"));
    const auto release = reinterpret_cast<Stroke>(dlsym(scope,"_ZN7YDALAPI11YOCRManager18onHeadButtonReleseEjb"));
    bool ok = parameters[0].is_bool() && sequence > 0 && instance && press && release;
    if (ok) {
      auto engine = instance(); ok = engine != nullptr;
      if (ok) (down ? press : release)(engine, unsigned(sequence), false);
    }
    if (module) dlclose(module);
    info.GetReturnValue().Set(ok);
  }
  void poll(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bson::array messages;
    for (int i = 0; i < 64 && fd_ >= 0; ++i) {
      char b[65537];
      ssize_t n = recv(fd_, b, 65536, MSG_DONTWAIT);
      if (n > 0) {
        std::string error;
        auto j = Bson::parse(std::string(b, n), error);
        if (error.empty())
          messages.push_back(j);
      } else if (n == 0) {
        close(fd_);
        fd_ = -1;
        break;
      } else
        break;
    }
    if (pid_ > 0) {
      int status = 0;
      auto result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD)) {
        pid_ = -1;
        messages.push_back(Bson::object{
            {"v", 1},
            {"type", "exit"},
            {"status", WIFEXITED(status) ? WEXITSTATUS(status)
                                         : 128 + WTERMSIG(status)}});
      }
    }
    info.GetReturnValue().Set(Bson(messages));
  }
  void stop(JQAsyncInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
    info.post(Bson::object{{"ok", true}});
  }
};
std::vector<std::string> exports = {"launcher"};
int init(JSContext *ctx, JSModuleDef *module) {
  auto env = JQModuleEnv::CreateModule(ctx, module, "penchatLauncher");
  auto tpl = JQFunctionTemplate::New(env.get(), "launcher");
  tpl->InstanceTemplate()->setObjectCreator([]() { return new Launcher; });
  tpl->SetProtoMethodPromise("start", &Launcher::start);
  tpl->SetProtoMethodPromise("install", &Launcher::install);
  tpl->SetProtoMethod("reopen", &Launcher::reopen);
  tpl->SetProtoMethod("send", &Launcher::send);
  tpl->SetProtoMethod("poll", &Launcher::poll);
  tpl->SetProtoMethod("ocrStroke", &Launcher::ocrStroke);
  tpl->SetProtoMethod("status", &Launcher::status);
  tpl->SetProtoMethodPromise("stop", &Launcher::stop);
  env->setModuleExport("launcher", tpl->CallConstructor());
  env->setModuleExportDone(JS_UNDEFINED, exports);
  return 0;
}
DEF_MODULE_LOAD_FUNC_EXPORT(penchatLauncher, init, exports)
} // namespace
extern "C" JQUICK_EXPORT void custom_init_jsapis() {
  registerCModuleLoader("penchatLauncher", &penchatLauncher_module_load);
}
extern "C" JQUICK_EXPORT int jsapi_check_unload() { return 0; }
